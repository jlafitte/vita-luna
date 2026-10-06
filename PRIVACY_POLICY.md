# Privacy Policy for vita-luna

**Effective Date:** October 6, 2026

`vita-luna` is an open-source, non-commercial homebrew application for the PlayStation Vita designed to enable cloud gaming streaming via Amazon Luna.

---

## 1. Data Collection & Processing
* **No Personal Data Collection**: `vita-luna` does not collect, store, transmit, or process any personal identification, telemetry, or analytics data.
* **Direct Amazon Authentication**: Authentication is performed directly between the PlayStation Vita application and official Amazon OAuth servers (`https://api.amazon.com`). No third-party servers, intermediate proxies, or analytics trackers are used.

## 2. Credentials & Token Storage
* **Local Storage Only**: Access tokens and refresh tokens received from Amazon are stored locally on the user's PlayStation Vita device (`ux0:data/vita-luna/tokens.dat`).
* **No Remote Telemetry**: Tokens and session credentials are never transmitted to any destination other than official Amazon Luna endpoints.

## 3. Open Source Transparency
`vita-luna` is open-source software. You can inspect the full source code, building procedures, and network communication handling in this repository.

## 4. Contact & Support
For questions or issues regarding `vita-luna`, please open an issue in this repository.
