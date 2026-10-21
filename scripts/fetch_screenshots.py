#!/usr/bin/env python3
import sys
import os
from ftplib import FTP

def fetch_screenshots(vita_ip, dest_dir="screenshots"):
    print(f"==> Checking for PS Vita screenshots on {vita_ip}...")
    os.makedirs(dest_dir, exist_ok=True)
    try:
        ftp = FTP()
        ftp.connect(vita_ip, 1337, timeout=10)
        ftp.login()
        ftp.set_pasv(True)
        
        screenshot_dirs = ["ux0:/picture/SCREENSHOT", "ux0:/picture"]
        shots_to_download = [] # list of (dir_path, filename)
        
        for base_dir in screenshot_dirs:
            try:
                ftp.cwd(base_dir)
                items = ftp.nlst()
                for item in items:
                    if item.lower().endswith(('.png', '.jpg', '.bmp')):
                        shots_to_download.append((base_dir, item))
                    else:
                        sub_dir = f"{base_dir}/{item}"
                        try:
                            ftp.cwd(sub_dir)
                            sub_files = ftp.nlst()
                            for sf in sub_files:
                                if sf.lower().endswith(('.png', '.jpg', '.bmp')):
                                    shots_to_download.append((sub_dir, sf))
                        except Exception:
                            pass
            except Exception:
                pass

        if not shots_to_download:
            print("-> No screenshot images found on PS Vita.")
            ftp.quit()
            return []

        print(f"-> Found {len(shots_to_download)} screenshot(s) on PS Vita:")
        saved_files = []
        for dir_path, shot_name in shots_to_download:
            local_path = os.path.join(dest_dir, shot_name)
            if not os.path.exists(local_path):
                print(f"   Downloading: {dir_path}/{shot_name} -> {local_path}...")
                ftp.cwd(dir_path)
                with open(local_path, "wb") as f:
                    ftp.retrbinary(f"RETR {shot_name}", f.write)
                print(f"   [DONE] Saved {local_path}")
                saved_files.append(local_path)
            else:
                saved_files.append(local_path)

        ftp.quit()
        return saved_files
    except Exception as e:
        print(f"Error fetching screenshots via FTP: {e}")
        return []

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 fetch_screenshots.py <VITA_IP> [DEST_DIR]")
        sys.exit(1)

    vita_ip = sys.argv[1]
    dest_dir = sys.argv[2] if len(sys.argv) > 2 else "screenshots"
    fetch_screenshots(vita_ip, dest_dir)
