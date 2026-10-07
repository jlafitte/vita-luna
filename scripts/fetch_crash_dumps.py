#!/usr/bin/env python3
import sys
import os
from ftplib import FTP

def fetch_crash_dumps(vita_ip, dest_dir="/Users/john/Desktop"):
    print(f"==> Checking for PS Vita crash dumps on {vita_ip}...")
    try:
        ftp = FTP()
        ftp.connect(vita_ip, 1337, timeout=10)
        ftp.login()
        ftp.set_pasv(True)
        ftp.cwd("ux0:/data")
        
        files = ftp.nlst()
        dumps = [f for f in files if "psp2core" in f or f.endswith(".psp2dmp")]
        
        if not dumps:
            print("-> No crash dumps found in ux0:data/.")
            ftp.quit()
            return

        print(f"-> Found {len(dumps)} crash dump file(s) in ux0:data/:")
        for dump_name in dumps:
            local_path = os.path.join(dest_dir, dump_name)
            if not os.path.exists(local_path):
                print(f"   Downloading new dump: {dump_name} -> {local_path}...")
                with open(local_path, "wb") as f:
                    ftp.retrbinary(f"RETR {dump_name}", f.write)
                print(f"   [DONE] Saved to {local_path}")
            else:
                print(f"   [EXISTS] {dump_name} already downloaded to {dest_dir}")
        ftp.quit()
    except Exception as e:
        print(f"Error fetching crash dumps via FTP: {e}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 fetch_crash_dumps.py <VITA_IP> [DEST_DIR]")
        sys.exit(1)
    
    vita_ip = sys.argv[1]
    dest_dir = sys.argv[2] if len(sys.argv) > 2 else "/Users/john/Desktop"
    fetch_crash_dumps(vita_ip, dest_dir)
