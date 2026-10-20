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

    # 2. Connect directly to VitaCompanion native FTP daemon (Port 1337)
    import time
    time.sleep(0.5)  # Allow process to terminate and release file handles
    ftp = FTP()
    ftp_connected = False

    for port in [1337, 21]:
        try:
            print(f"-> Connecting to VitaCompanion FTP daemon at {vita_ip}:{port}...")
            ftp.connect(vita_ip, port, timeout=5)
            ftp.login()
            ftp.set_pasv(True)
            ftp_connected = True
            print(f"-> Connected to FTP on port {port} ({ftp.getwelcome().strip()}).")
            break
        except Exception as e:
            pass

    if not ftp_connected:
        print(f"Error: Could not connect to FTP daemon on {vita_ip} (tested ports 1337, 21).")
        print("Ensure VitaCompanion is running and Wi-Fi is connected.")
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

    # Sync sce_sys assets to ux0:/app/{title_id}/sce_sys/ and ur0:/appmeta/{title_id}/
    project_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    sce_sys_dir = os.path.join(project_root, "sce_sys")
    if os.path.exists(sce_sys_dir):
        print(f"-> Syncing LiveArea assets to ux0:/app/{title_id}/sce_sys/ and ur0:/appmeta/{title_id}/...")
        for base_dest in [f"ux0:/app/{title_id}/sce_sys", f"ur0:/appmeta/{title_id}"]:
            for root, dirs, files in os.walk(sce_sys_dir):
                rel_dir = os.path.relpath(root, sce_sys_dir)
                remote_dir = base_dest if rel_dir == "." else f"{base_dest}/{rel_dir.replace(os.sep, '/')}"
                try:
                    ftp.mkd(remote_dir)
                except Exception:
                    pass
                for file in files:
                    local_file = os.path.join(root, file)
                    remote_file = f"{remote_dir}/{file}"
                    try:
                        with open(local_file, 'rb') as f:
                            ftp.storbinary(f"STOR {remote_file}", f)
                    except Exception as e:
                        print(f"   Note syncing {file}: {e}")
        print("-> LiveArea assets sync complete!")

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
