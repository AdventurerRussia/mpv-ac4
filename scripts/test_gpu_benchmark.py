import os
import sys
import time
import subprocess
import threading
import numpy as np

VIDEO_PATH = r"R:\test_4k_30s.mkv"
FFMPEG = r"R:\install\bin\ffmpeg.exe"
MPV = r"R:\MPV PLAYER AC-4\mpv.com"

ENV = dict(os.environ)
ENV["PATH"] = r"R:\MPV PLAYER AC-4;R:\msys64\ucrt64\bin;" + ENV.get("PATH", "")

def get_gpu_status():
    try:
        res = subprocess.run(
            ["nvidia-smi", "--query-gpu=utilization.gpu,memory.used,power.draw", "--format=csv,noheader,nounits"],
            capture_output=True, text=True, timeout=5
        )
        parts = [p.strip() for p in res.stdout.strip().split(",")]
        return {
            "gpu_util": float(parts[0]),
            "vram_used": float(parts[1]),
            "power_draw": float(parts[2])
        }
    except Exception as e:
        return {"gpu_util": 0.0, "vram_used": 0.0, "power_draw": 0.0}

class GPUMonitor(threading.Thread):
    def __init__(self, interval=0.1):
        super().__init__()
        self.interval = interval
        self.running = True
        self.samples = []

    def run(self):
        while self.running:
            self.samples.append(get_gpu_status())
            time.sleep(self.interval)

    def stop(self):
        self.running = False
        self.join()

    def stats(self):
        if not self.samples:
            return {"max_util": 0, "avg_util": 0, "max_vram": 0, "max_power": 0}
        utils = [s["gpu_util"] for s in self.samples]
        vrams = [s["vram_used"] for s in self.samples]
        powers = [s["power_draw"] for s in self.samples]
        return {
            "max_util": max(utils),
            "avg_util": sum(utils) / len(utils),
            "max_vram": max(vrams),
            "max_power": max(powers),
            "count": len(self.samples)
        }

def run_decode_test(decoder_name, num_frames=120):
    print(f"\n=======================================================")
    print(f"  Testing decoder: {decoder_name} ({num_frames} frames, 4K 3840x2160)")
    print(f"=======================================================")

    cmd = [
        FFMPEG, "-vcodec", decoder_name,
        "-i", VIDEO_PATH,
        "-vframes", str(num_frames),
        "-f", "null", "-"
    ]

    monitor = GPUMonitor(interval=0.05)
    monitor.start()

    start_t = time.perf_counter()
    p = subprocess.run(cmd, capture_output=True, text=True, env=ENV)
    elapsed = time.perf_counter() - start_t

    monitor.stop()

    if p.returncode != 0:
        print(f"ERROR: process failed with return code {p.returncode}")
        print(p.stderr[-1000:])
        return None

    fps = num_frames / elapsed
    st = monitor.stats()

    print(f"  -> Time elapsed:     {elapsed:.2f} s")
    print(f"  -> Decoding speed:   {fps:.1f} FPS")
    print(f"  -> Max GPU Util:     {st['max_util']:.1f} %")
    print(f"  -> Avg GPU Util:     {st['avg_util']:.1f} %")
    print(f"  -> Peak VRAM Used:   {st['max_vram']:.0f} MB")
    print(f"  -> Peak Power Draw:  {st['max_power']:.1f} W")

    return {
        "decoder": decoder_name,
        "elapsed": elapsed,
        "fps": fps,
        "gpu_stats": st
    }

def run_mpv_test(num_frames=100):
    print(f"\n=======================================================")
    print(f"  Testing mpv player with --vd=jpeg2000_nvjpeg (4K playback)")
    print(f"=======================================================")

    cmd = [
        MPV,
        "--vd=jpeg2000_nvjpeg",
        VIDEO_PATH,
        f"--frames={num_frames}",
        "--vo=null",
        "--msg-level=all=v"
    ]

    monitor = GPUMonitor(interval=0.05)
    monitor.start()

    start_t = time.perf_counter()
    p = subprocess.run(cmd, capture_output=True, text=True)
    elapsed = time.perf_counter() - start_t

    monitor.stop()

    fps = num_frames / elapsed
    st = monitor.stats()

    print(f"  -> mpv decode time:  {elapsed:.2f} s ({num_frames} frames)")
    print(f"  -> mpv decode speed: {fps:.1f} FPS")
    print(f"  -> Max GPU Util:     {st['max_util']:.1f} %")
    print(f"  -> Peak VRAM Used:   {st['max_vram']:.0f} MB")
    print(f"  -> Peak Power Draw:  {st['max_power']:.1f} W")

    # Check for success messages in log
    success_init = "nvjpeg2k: GPU JPEG 2000 decoder initialised successfully" in p.stdout
    print(f"  -> GPU init confirmed in mpv: {success_init}")

def run_fidelity_check():
    print(f"\n=======================================================")
    print(f"  Verifying Visual & Mathematical Fidelity (PSNR)")
    print(f"=======================================================")

    # Decode 1 frame to PNG with CPU
    f_cpu = r"R:\test_4k_cpu.png"
    f_gpu = r"R:\test_4k_gpu.png"

    subprocess.run([FFMPEG, "-vcodec", "jpeg2000", "-i", VIDEO_PATH, "-vframes", "1", f_cpu, "-y"], capture_output=True, env=ENV)
    subprocess.run([FFMPEG, "-vcodec", "jpeg2000_nvjpeg", "-i", VIDEO_PATH, "-vframes", "1", f_gpu, "-y"], capture_output=True, env=ENV)

    # Compute PSNR via ffmpeg
    res = subprocess.run([FFMPEG, "-i", f_cpu, "-i", f_gpu, "-filter_complex", "psnr", "-f", "null", "-"], capture_output=True, text=True, env=ENV)
    for line in res.stderr.splitlines():
        if "PSNR" in line:
            print("  -> " + line.strip())

    for f in [f_cpu, f_gpu]:
        if os.path.exists(f):
            os.remove(f)

if __name__ == "__main__":
    baseline = get_gpu_status()
    print("Baseline GPU status:")
    print(f"  GPU Util: {baseline['gpu_util']}% | VRAM: {baseline['vram_used']} MB | Power: {baseline['power_draw']} W")

    # 1. Fidelity check
    run_fidelity_check()

    # 2. CPU vs GPU Benchmark on 120 frames of 4K
    gpu_res = run_decode_test("jpeg2000_nvjpeg", num_frames=120)
    cpu_res = run_decode_test("jpeg2000", num_frames=120)

    if gpu_res and cpu_res:
        speedup = gpu_res["fps"] / cpu_res["fps"]
        print(f"\n=======================================================")
        print(f"  BENCHMARK SUMMARY (4K 3840x2160, 120 frames):")
        print(f"  - CPU Decoder:     {cpu_res['fps']:.1f} FPS ({cpu_res['elapsed']:.2f} s)")
        print(f"  - NVIDIA GPU (nvJPEG2000): {gpu_res['fps']:.1f} FPS ({gpu_res['elapsed']:.2f} s)")
        print(f"  - GPU Speedup:     {speedup:.2f}x FASTER!")
        print(f"  - GPU Peak Core Load: {gpu_res['gpu_stats']['max_util']:.1f} %")
        print(f"  - GPU Peak Power:  {gpu_res['gpu_stats']['max_power']:.1f} W")
        print(f"=======================================================")

    # 3. mpv test
    run_mpv_test(num_frames=120)
