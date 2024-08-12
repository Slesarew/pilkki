mod command;
use command::{
    cmd, connect, erase, erase_eflash, get_buf_size, get_loader_info, read_c, read_crc,
    read_eflash, simple_command, write_c, write_eflash, FLASH_USERDATA_ADDRESS,
};

mod ecc;

mod serial;
use serial::connect_port;

use clap::{arg, command, Command};

fn main() {
    let matches = command!()
        .subcommand_required(true)
        .arg(arg!(-p --port <PORT> "Serial port to interact with"))

        .subcommand(Command::new("connect")
            .about("Connect to the target and halt it"))

        .subcommand(Command::new("crc")
            .about("Calculate CRC32 checksum of memory region on the target")
            .arg(arg!(-a --addr <ADDRESS> "Starting address on the target. Will be alligned to words. [default: 0x08000000]"))
            .arg(arg!(-l --length <LENGTH> "Length of the memory region to read (in bytes). Will be alligned to words."))
            )

        .subcommand(Command::new("erase")
            .about("Erase firmware from the target.")
            .arg(arg!(-a --addr <ADDRESS> "Starting address on the target. Will be aligned to pages. [default: 0x08000000]"))
            .arg(arg!(-n --pages <PAGES> "Number of pages to erase. [default: all]"))
            )

        .subcommand(Command::new("erase-eflash")
            .about("Erase external flash on the target.")
            .arg(arg!(-a --addr <ADDRESS> "Starting address on the target. Will be aligned to pages. [default: 0x00000000]"))
            .arg(arg!(-n --pages <PAGES> "Number of pages to erase. [default: all]"))
            )

        .subcommand(Command::new("halt")
            .about("Halt the target"))

        .subcommand(Command::new("id")
            .about("Get SWD Programmer Hardware ID"))

        .subcommand(Command::new("read")
            .about("Read firmware from the target.")
            .arg(arg!(-a --addr <ADDRESS> "Starting address on the target. [default: 0x08000000]"))
            .arg(arg!(-o --output <FILENAME> "Output file name [default: out.bin]"))
            .arg(arg!(-l --length <LENGTH> "Length of the memory region to read (in bytes). [default: memory_size]"))
            )

        .subcommand(Command::new("read-eflash")
            .about("Read external flash on the target.")
            .arg(arg!(-a --addr <ADDRESS> "Starting address on the target. [default: 0x00000000]"))
            .arg(arg!(-o --output <FILENAME> "Output file name [default: out-eflash.bin]"))
            .arg(arg!(-l --length <LENGTH> "Length of the memory region to read (in bytes). [default: memory_size]"))
            )

        .subcommand(Command::new("reset")
            .about("Reset the target (soft/hard)")
            .arg(arg!(-r --hard "Hard reset. (default: false)"))
            )

        .subcommand(Command::new("run")
            .about("Run the target"))

        .subcommand(Command::new("write")
            .about("Write firmware to the target.")
            .arg(arg!(-a --addr <ADDRESS> "Starting address on the target. [default: 0x08000000]"))
            .arg(arg!(-i --input <FILENAME> "Input file name (required)").required(true))
            .arg(arg!(-l --length <LENGTH> "Length of the memory region to write (in bytes). [default: memory_size]"))
            )

        .subcommand(Command::new("write-eflash")
            .about("Write image to external flash on the target.")
            .arg(arg!(-a --addr <ADDRESS> "Starting address on the target's eflash. Will be aligned to pages. [default: 0x00000000]"))
            .arg(arg!(-i --input <FILENAME> "Input file name (required)").required(true))
            .arg(arg!(-l --length <LENGTH> "Length of the memory region to write (in bytes). Will be aligned to pages. [default: eflash_memory_size]"))
            )

        .get_matches();

    //println!("{:?}", matches);
    let port_name = matches.get_one::<String>("port");

    let mut port = connect_port(port_name).unwrap();

    let info = match connect(&mut port) {
        Ok(info) => info,
        Err(message) => return println!("{}", message),
    };

    let loader_info = match matches.subcommand() {
        Some(("read-eflash", _)) | Some(("write-eflash", _)) | Some(("erase-eflash", _)) => {
            match get_loader_info(&mut port) {
                Ok(loader_info) => {
                    if loader_info.get("SPIFlashSize").is_none() {
                        return println!("Flasher does not seem to support this command, try to update the firmware");
                    }
                    Some(loader_info)
                }
                Err(message) => {
                    return println!("{}", message);
                }
            }
        }
        Some(("write", _)) | Some(("erase", _)) => {
            match get_loader_info(&mut port) {
                Ok(mut loader_info) => {
                    if loader_info.get("MaxBufSize").is_none() {
                        //legacy support
                        loader_info.insert(
                            "MaxBufSize".to_string(),
                            get_buf_size(&mut port).to_string(),
                        );
                    }
                    Some(loader_info)
                }
                Err(message) => {
                    return println!("{}", message);
                }
            }
        }
        _ => None,
    };

    match matches.subcommand() {
        Some(("connect", _)) => {
            for (key, value) in info {
                println!("{}: {}", key, value);
            }
        }

        Some(("crc", sub_matches)) => {
            let addr = if let Some(a) = sub_matches.get_one::<String>("addr") {
                u32_from_hex_dec_str(a)
            } else {
                0x08000000
            };
            let len = sub_matches
                .get_one::<String>("length")
                .map(|a| u32_from_hex_dec_str(a));
            read_crc(&mut port, info, addr, len);
        }

        Some(("erase", sub_matches)) => {
            let addr = if let Some(a) = sub_matches.get_one::<String>("addr") {
                u32_from_hex_dec_str(a)
            } else {
                0x08000000
            };
            let len = sub_matches
                .get_one::<String>("pages")
                .map(|a| u32_from_hex_dec_str(a));
            erase(&mut port, info, loader_info.unwrap(), addr, len);
        }

        Some(("erase-eflash", sub_matches)) => {
            let addr = if let Some(a) = sub_matches.get_one::<String>("addr") {
                u32_from_hex_dec_str(a)
            } else {
                0x00000000
            };
            let len = sub_matches
                .get_one::<String>("pages")
                .map(|a| u32_from_hex_dec_str(a));
            erase_eflash(&mut port, loader_info.unwrap(), addr, len);
        }

        Some(("halt", _)) => {
            let req = "\nhalt\n".as_bytes();
            let res = simple_command(&mut port, req);
            println!("{}", String::from_utf8(res.clone()).unwrap());
        }

        Some(("id", _)) => {
            let req = "\nid\n".as_bytes();
            let res = simple_command(&mut port, req);
            println!("{}", String::from_utf8(res.clone()).unwrap());
        }

        Some(("read", sub_matches)) => {
            let addr = if let Some(a) = sub_matches.get_one::<String>("addr") {
                u32_from_hex_dec_str(a)
            } else {
                FLASH_USERDATA_ADDRESS
            };
            let len = sub_matches
                .get_one::<String>("length")
                .map(|a| u32_from_hex_dec_str(a));
            let filename = if let Some(a) = sub_matches.get_one::<String>("output") {
                a
            } else {
                "out.bin"
            };
            std::fs::write(filename, read_c(&mut port, info, addr, len)).unwrap();
        }

        Some(("read-eflash", sub_matches)) => {
            let addr = if let Some(a) = sub_matches.get_one::<String>("addr") {
                u32_from_hex_dec_str(a)
            } else {
                0x00000000
            };
            let len = sub_matches
                .get_one::<String>("length")
                .map(|a| u32_from_hex_dec_str(a));
            let filename = if let Some(a) = sub_matches.get_one::<String>("output") {
                a
            } else {
                "out-eflash.bin"
            };
            std::fs::write(
                filename,
                read_eflash(&mut port, loader_info.unwrap(), addr, len),
            )
            .unwrap();
        }

        Some(("reset", sub_matches)) => {
            let req = if sub_matches.get_flag("hard") {
                cmd("reset -h")
            } else {
                cmd("reset")
            };
            let res = simple_command(&mut port, &req);
            println!("{}", String::from_utf8(res.clone()).unwrap());
        }

        Some(("run", _)) => {
            let req = "\nrun\n".as_bytes();
            let res = simple_command(&mut port, req);
            println!("{}", String::from_utf8(res.clone()).unwrap());
        }

        Some(("write", sub_matches)) => {
            let addr = if let Some(a) = sub_matches.get_one::<String>("addr") {
                u32_from_hex_dec_str(a)
            } else {
                0x08000000
            };
            let len = sub_matches
                .get_one::<String>("length")
                .map(|a| u32_from_hex_dec_str(a));
            let filename = if let Some(a) = sub_matches.get_one::<String>("input") {
                a
            } else {
                unreachable!();
            };
            write_c(
                &mut port,
                info,
                loader_info.unwrap(),
                std::fs::read(filename).unwrap(),
                addr,
                len,
            );
        }

        Some(("write-eflash", sub_matches)) => {
            let addr = if let Some(a) = sub_matches.get_one::<String>("addr") {
                u32_from_hex_dec_str(a)
            } else {
                0x00000000
            };
            let len = sub_matches
                .get_one::<String>("length")
                .map(|a| u32_from_hex_dec_str(a));
            let filename = if let Some(a) = sub_matches.get_one::<String>("input") {
                a
            } else {
                unreachable!();
            };
            write_eflash(
                &mut port,
                loader_info.unwrap(),
                std::fs::read(filename).unwrap(),
                addr,
                len,
            );
        }

        None => unreachable!(),

        _ => (),
    }
}

fn u32_from_hex_dec_str(a: &str) -> u32 {
    if let Some(a) = a.strip_prefix("0x") {
        u32::from_str_radix(a, 16).expect("address not a hex value")
    } else {
        u32::from_str_radix(a, 10).expect("address not a dec value")
    }
}
