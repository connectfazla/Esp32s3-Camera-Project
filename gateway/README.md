# Private remote access with Raspberry Pi and Tailscale

This gateway exposes HomeCam only to devices signed into your Tailscale
network. It does not make the camera public and requires no router port
forwarding.

## What you need

- Raspberry Pi or another always-on Debian/Ubuntu Linux machine
- Tailscale installed on that machine and your phone
- A DHCP reservation for the ESP32, for example `192.168.1.50`

## 1. Install the gateway software

On the Raspberry Pi:

```bash
sudo apt update
sudo apt install nginx
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up
```

## 2. Configure the HomeCam proxy

Edit `homecam.nginx.conf` and replace `192.168.1.50` with the ESP32's reserved
LAN address. Then install it:

```bash
sudo cp homecam.nginx.conf /etc/nginx/conf.d/homecam.conf
sudo nginx -t
sudo systemctl reload nginx
```

Confirm the combined gateway locally:

```bash
curl -I http://127.0.0.1:8080/
```

## 3. Publish privately with Tailscale Serve

```bash
sudo tailscale serve --bg 8080
sudo tailscale serve status
```

The status command prints a private HTTPS URL similar to:

```text
https://your-pi.your-tailnet.ts.net
```

Install and sign in to Tailscale on the phone, then open that URL. Tailscale
Serve persists across reboots when configured with `--bg`.

## Security notes

- Keep Nginx bound to `127.0.0.1`; do not change it to `0.0.0.0`.
- Do not enable **Tailscale Funnel**. Funnel is for public internet exposure.
- Restrict access to the Pi in your tailnet policy if other people share the
  same tailnet.
- Do not forward ESP32 ports 80 or 81 on the home router.
- The dashboard automatically uses the gateway's same-origin `/stream` path
  when opened over HTTPS, avoiding mixed-content errors.

To disable remote serving later:

```bash
sudo tailscale serve reset
```

