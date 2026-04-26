//! m32_flash — Arduino IDE uploader for Native Instruments Komplete Kontrol M32.
//!
//! Subcommands:
//!   flash <file.bin>   sanity-check image, optionally auto-reboot device into DFU
//!                      via 1200 bps serial touch, then flash via libusb DFU.
//!   restore-factory    flash the stock NI app firmware bundled at
//!                      <exe_dir>/firmware/m32_ni_factory.bin (rollback).
//!   probe              list matching USB devices.
//!
//! Safety rails (always on):
//!   * Refuses to flash anything whose first two vector-table words don't look like
//!     (SP in 0x20000000..0x20005000, Reset_Handler in 0x08003000..0x0801FFFF).
//!   * Refuses images larger than the app partition (116 KB).
//!   * Only ever talks to VID:PID 0x17CC:0x1862 in DFU mode.
//!   * Writes start at offset 0x08003000 — the NI bootloader is never touched.

use anyhow::{anyhow, bail, Context as _, Result};
use clap::{Parser, Subcommand};
use rusb::UsbContext;
use std::path::PathBuf;
use std::time::{Duration, Instant};

const VID: u16 = 0x17CC;
const DFU_PID: u16 = 0x1862;

const APP_FLASH_BASE: u32 = 0x0800_3000;
const APP_PARTITION_END: u32 = 0x0802_0000;
const APP_MAX_SIZE: usize = (APP_PARTITION_END - APP_FLASH_BASE) as usize;
const RAM_BASE: u32 = 0x2000_0000;
const RAM_END: u32 = 0x2000_5000;

const DFU_INTF: u8 = 0;
const DFU_ALT: u8 = 0;

#[derive(Parser)]
#[command(about, version)]
struct Cli {
    /// Verbose progress output.
    #[arg(short, long)]
    verbose: bool,

    #[command(subcommand)]
    cmd: Cmd,
}

#[derive(Subcommand)]
enum Cmd {
    /// Flash a .bin built for offset 0x08003000.
    Flash {
        /// Path to the firmware .bin.
        file: PathBuf,
    },
    /// Write the bundled NI factory firmware (full rollback).
    RestoreFactory,
    /// List USB devices and report whether the M32 is currently in DFU mode.
    Probe,
}

fn main() {
    let cli = Cli::parse();
    if let Err(e) = run(&cli) {
        eprintln!("error: {e:#}");
        std::process::exit(1);
    }
}

fn run(cli: &Cli) -> Result<()> {
    match &cli.cmd {
        Cmd::Flash { file } => cmd_flash(file, cli.verbose),
        Cmd::RestoreFactory => {
            let factory = factory_firmware_path()?;
            println!("Restoring NI factory firmware from {}", factory.display());
            cmd_flash(&factory, cli.verbose)
        }
        Cmd::Probe => cmd_probe(),
    }
}

// ---------------------------------------------------------------- image validation

fn validate_image(path: &PathBuf) -> Result<Vec<u8>> {
    let data = std::fs::read(path).with_context(|| format!("reading {}", path.display()))?;
    if data.len() < 8 {
        bail!("file too small ({} B), not a firmware image", data.len());
    }
    if data.len() > APP_MAX_SIZE {
        bail!(
            "image too large: {} B > {} B max app partition",
            data.len(),
            APP_MAX_SIZE
        );
    }
    let sp = u32::from_le_bytes(data[0..4].try_into().unwrap());
    let rv = u32::from_le_bytes(data[4..8].try_into().unwrap());
    if !(RAM_BASE..=RAM_END).contains(&sp) {
        bail!(
            "initial SP 0x{sp:08X} not in RAM [0x{RAM_BASE:08X}..0x{RAM_END:08X}] — \
             did you link at 0x08000000 instead of 0x{APP_FLASH_BASE:08X}?"
        );
    }
    if !(APP_FLASH_BASE..APP_PARTITION_END).contains(&rv) {
        bail!(
            "Reset_Handler 0x{rv:08X} not in app partition [0x{APP_FLASH_BASE:08X}..0x{APP_PARTITION_END:08X}] — \
             link with FLASH ORIGIN = 0x{APP_FLASH_BASE:08X}"
        );
    }
    Ok(data)
}

// ---------------------------------------------------------------- USB enumeration

fn is_m32_in_dfu() -> bool {
    let Ok(devices) = rusb::devices() else {
        return false;
    };
    devices.iter().any(|d| match d.device_descriptor() {
        Ok(desc) => desc.vendor_id() == VID && desc.product_id() == DFU_PID,
        Err(_) => false,
    })
}

// ---------------------------------------------------------------- wait for DFU mode

fn wait_for_dfu_interactive() -> Result<()> {
    if is_m32_in_dfu() {
        return Ok(());
    }
    println!();
    println!("┌───────────────────────────────────────────────────────────────────┐");
    println!("│ M32 not in DFU mode.                                              │");
    println!("│ Put it in DFU manually:                                           │");
    println!("│   1. Unplug USB.                                                  │");
    println!("│   2. Hold the forced-DFU button combo (Shift + your combo).       │");
    println!("│   3. Plug USB while holding.                                      │");
    println!("│                                                                   │");
    println!("│ Waiting for USB 0x17CC:0x1862 to appear…                          │");
    println!("└───────────────────────────────────────────────────────────────────┘");
    let deadline = Instant::now() + Duration::from_secs(120);
    while Instant::now() < deadline {
        if is_m32_in_dfu() {
            println!("✓ DFU device detected.");
            return Ok(());
        }
        std::thread::sleep(Duration::from_millis(300));
    }
    bail!("timed out waiting for M32 in DFU mode");
}

// ---------------------------------------------------------------- flash

fn open_dfu_device(
    context: &rusb::Context,
) -> Result<dfu_libusb::Dfu<rusb::Context>> {
    // Find the M32 in DFU mode manually so we can detach the kernel driver
    // before claim_interface (otherwise libusb_claim_interface returns
    // LIBUSB_ERROR_ACCESS on macOS, where the IOUSB family holds a claim by
    // default).
    let device = context
        .devices()
        .context("listing USB devices")?
        .iter()
        .find(|d: &rusb::Device<rusb::Context>| {
            d.device_descriptor()
                .map(|desc| desc.vendor_id() == VID && desc.product_id() == DFU_PID)
                .unwrap_or(false)
        })
        .ok_or_else(|| anyhow!("no DFU device found ({VID:04x}:{DFU_PID:04x})"))?;

    let handle = device.open().context("opening USB device handle")?;
    // No-op on Windows; on Linux/macOS this lets us claim the interface even
    // when the OS already attached a kernel driver to it.
    let _ = handle.set_auto_detach_kernel_driver(true);

    dfu_libusb::DfuLibusb::from_usb_device(device, handle, DFU_INTF, DFU_ALT)
        .context("attaching DFU protocol to device")
}

fn flash_via_dfu(image: &[u8], verbose: bool) -> Result<()> {
    let context = rusb::Context::new().context("creating libusb context")?;
    let mut device = open_dfu_device(&context)?;

    // The bootloader's alt 0 is declared at 0x08000000 ("@Internal Flash …"),
    // but the application partition starts at 0x08003000. Force the DfuSe
    // download address to APP_FLASH_BASE so we never write over the bootloader,
    // regardless of whether the bootloader honours DfuSe strictly.
    device.override_address(APP_FLASH_BASE);

    let total = image.len();
    println!(
        "Flashing {} bytes to 0x{:08X} via USB DFU ({VID:04x}:{DFU_PID:04x})",
        total, APP_FLASH_BASE
    );
    if verbose {
        println!("  · DFU device opened: alt={DFU_ALT}");
    }

    // Arduino IDE 2.x's console doesn't honour `\r`, so we print one progress
    // line per 10% step instead of an in-place bar.
    let mut next_pct = 10usize;
    device.with_progress(move |bytes_done| {
        let pct = (bytes_done * 100) / total.max(1);
        while pct >= next_pct && next_pct <= 100 {
            println!("  ... {next_pct:3}% ({bytes_done}/{total} B)");
            next_pct += 10;
        }
    });

    device
        .download_from_slice(image)
        .context("DFU download failed")?;
    Ok(())
}

// ---------------------------------------------------------------- subcommands

fn cmd_flash(path: &PathBuf, verbose: bool) -> Result<()> {
    let image = validate_image(path)?;
    if verbose {
        println!("  · image OK: {} B", image.len());
    }
    if !is_m32_in_dfu() {
        wait_for_dfu_interactive()?;
    }
    flash_via_dfu(&image, verbose)?;
    println!("✓ Flash complete. M32 is rebooting.");
    Ok(())
}

fn cmd_probe() -> Result<()> {
    println!("USB devices visible to libusb:");
    let devices = rusb::devices().context("rusb::devices")?;
    let mut found_dfu = false;
    for d in devices.iter() {
        let Ok(desc) = d.device_descriptor() else { continue };
        let mark = if desc.vendor_id() == VID && desc.product_id() == DFU_PID {
            found_dfu = true;
            "  ← M32-DFU"
        } else if desc.vendor_id() == VID {
            "  ← NI device (runtime)"
        } else {
            ""
        };
        println!(
            "  {:04x}:{:04x}{}",
            desc.vendor_id(),
            desc.product_id(),
            mark
        );
    }
    println!();
    println!("M32 in DFU mode: {found_dfu}");
    Ok(())
}

fn factory_firmware_path() -> Result<PathBuf> {
    let exe = std::env::current_exe().context("locating own executable")?;
    let dir = exe
        .parent()
        .ok_or_else(|| anyhow!("executable has no parent dir: {}", exe.display()))?;
    let candidate = dir.join("firmware").join("m32_ni_factory.bin");
    if !candidate.exists() {
        bail!("factory firmware not found at {}", candidate.display());
    }
    Ok(candidate)
}
