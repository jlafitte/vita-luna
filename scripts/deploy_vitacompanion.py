#!/usr/bin/env python3
import sys
import os
import socket
from ftplib import FTP

def deploy(vita_ip, vpk_path, title_id="LUNA00001"):
    if not os.path.exists(vpk_path):
        print(f"Error: VPK file not found at {vpk_path}")
        sys.exit(1)

    vpk_filename = os.path.basename(vpk_path)
    print(f"==> Deploying {vpk_path} to PS Vita ({vita_ip})...")

    def send_cmd(ip, port, cmd):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(5)
            s.connect((ip, port))
            s.sendall(f"{cmd}\n".encode())
            resp = ""
            try:
                resp = s.recv(1024).decode('utf-8', errors='ignore').strip()
            except Exception:
                pass
            s.close()
            return True, resp
        except Exception as e:
            return False, str(e)

    # 1. Kill running app instance via VitaCompanion (Port 1338)
    print(f"-> Terminating active instance of {title_id}...")
    send_cmd(vita_ip, 1338, f"kill {title_id}")

    # 2. FTP Connection & Upload (eboot.bin hot-reload + VPK)
    ftp_ports = [1337, 21]
    ftp_connected = False
    ftp = FTP()

    for port in ftp_ports:
        try:
            print(f"-> Connecting to FTP at {vita_ip}:{port}...")
            ftp.connect(vita_ip, port, timeout=10)
            ftp.login()
            ftp.set_pasv(True)
            ftp_connected = True
            print(f"-> Connected to FTP on port {port}.")
            break
        except Exception as e:
            print(f"   Port {port} unavailable ({e})")

    if not ftp_connected:
        print("Error: Could not connect to FTP server on PS Vita. Ensure Wi-Fi is connected.")
        sys.exit(1)

    eboot_path = os.path.join(os.path.dirname(vpk_path), "vita-luna.self")
    if not os.path.exists(eboot_path):
        eboot_path = os.path.join(os.path.dirname(vpk_path), "eboot.bin")

    # Fast eboot.bin hot-reload directly into ux0:app/LUNA00001/
    eboot_uploaded = False
    if os.path.exists(eboot_path):
        try:
            print(f"-> Hot-reloading executable directly to ux0:/app/{title_id}/eboot.bin...")
            ftp.cwd(f"ux0:/app/{title_id}")
            with open(eboot_path, 'rb') as f:
                ftp.storbinary("STOR eboot.bin", f)
            eboot_uploaded = True
            print("-> Hot-reload executable update complete!")
        except Exception as e:
            print(f"   Note updating ux0:/app/{title_id}/eboot.bin: {e}")

    # Upload full VPK to ux0:data/
    print(f"-> Uploading VPK to ux0:/data/{vpk_filename}...")
    try:
        ftp.cwd("ux0:/data")
    except Exception:
        pass

    with open(vpk_path, 'rb') as f:
        ftp.storbinary(f"STOR {vpk_filename}", f)
    ftp.quit()
    print("-> Upload completed successfully!")

    # 3. Launch App via VitaCompanion (Port 1338)
    import time
    time.sleep(0.5)
    print(f"-> Launching {title_id} on PS Vita screen...")
    ok, resp = send_cmd(vita_ip, 1338, f"launch {title_id}")
    print(f"-> Launch Command Sent (Response: {resp})")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 deploy_vitacompanion.py <VITA_IP> [VPK_PATH]")
        sys.exit(1)

    vita_ip = sys.argv[1]
    vpk_path = sys.argv[2] if len(sys.argv) > 2 else "build/vita-luna.vpk"
    deploy(vita_ip, vpk_path)
