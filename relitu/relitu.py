import re
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

csv_path = r"pink_20260401_125509_Channel 1_fft_vs_time.csv"

with open(csv_path, "r", encoding="utf-8") as f:
    meta = f.readline().strip()

def pick(name, s, cast=float, default=None):
    m = re.search(rf"{name}\s*=\s*([^,\s]+)", s)
    if not m:
        return default
    try:
        return cast(m.group(1))
    except Exception:
        return default

value_type = str(pick("valueType", meta, str, default="unknown")).strip().lower()

df = pd.read_csv(csv_path, comment="#")
freq = df["freq_hz"].to_numpy(dtype=float)
time_cols = [c for c in df.columns if c != "freq_hz"]
time = np.array([float(c) for c in time_cols], dtype=float)
S_ft = df[time_cols].to_numpy(dtype=float)  # [F, T]

# 去掉 0 Hz（log轴不能有0）
mask = freq > 0
freq = freq[mask]
S_ft = S_ft[mask, :]

fig, ax = plt.subplots(figsize=(12, 6))

if value_type == "db":
    vmin, vmax = 0, 100
    cbar_label = "Level (dB SPL)"
else:
    vmin, vmax = np.nanpercentile(S_ft, 1), np.nanpercentile(S_ft, 99)
    cbar_label = "Magnitude (Linear)"

im = ax.imshow(
    S_ft,
    origin="lower",
    aspect="auto",
    cmap="jet",
    extent=[time.min(), time.max(), freq.min(), freq.max()],
    vmin=vmin,
    vmax=vmax
)

ax.set_yscale("log")
ax.set_xlabel("Time (s)")
ax.set_ylabel("Frequency (Hz, log)")
ax.set_title(f"FFT vs Time Spectrogram ({value_type})")

fig.colorbar(im, ax=ax, label=cbar_label)
plt.tight_layout()
plt.show()