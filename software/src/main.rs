mod command;
use command::{cmd, connect, erase, read_c, read_crc, simple_command, write_c};

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
            .arg(arg!(-a --addr <ADDRESS> "Starting address on the target. [default: 0x08000000]"))
            .arg(arg!(-l --length <LENGTH> "Length of the memory region to read (in bytes)."))
            )

        .subcommand(Command::new("erase")
            .about("Erase firmware from the target.")
            .arg(arg!(-a --addr <ADDRESS> "Starting address on the target. [default: 0x08000000]"))
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

        .get_matches();

    //println!("{:?}", matches);
    let port_name = matches.get_one::<String>("port");

    let mut port = connect_port(port_name).unwrap();

    match matches.subcommand() {
        Some(("connect", _)) => {
            let res = connect(&mut port);
            println!("{}", String::from_utf8(res.clone()).unwrap());
        }

        Some(("crc", sub_matches)) => {
            let addr = if let Some(a) = sub_matches.get_one::<String>("addr") {
                a.parse().unwrap()
            } else {
                0x08000000
            };
            let (len_text, len) = if let Some(a) = sub_matches.get_one::<String>("length") {
                let value = a.parse().unwrap();
                let len_text = format!("CRC of {} bytes", value);
                (len_text, Some(value))
            } else {
                let len_text = "CRC of available memory".to_string();
                (len_text, None)
            };
            println!(
                "{} starting from 0x{:08X} is 0x{:08X}",
                len_text,
                addr,
                read_crc(&mut port, addr, len)
            );
        }

        Some(("erase", sub_matches)) => {
            let addr = if let Some(a) = sub_matches.get_one::<String>("addr") {
                a.parse().unwrap()
            } else {
                0x08000000
            };
            let len = if let Some(a) = sub_matches.get_one::<String>("pages") {
                let value = a.parse().unwrap();
                Some(value)
            } else {
                None
            };
            erase(&mut port, addr, len);
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
                a.parse().unwrap()
            } else {
                0x08000000
            };
            let len = sub_matches
                .get_one::<String>("length")
                .map(|a| a.parse().unwrap());
            let filename = if let Some(a) = sub_matches.get_one::<String>("output") {
                a
            } else {
                "out.bin"
            };
            std::fs::write(filename, read_c(&mut port, addr, len)).unwrap();
        }

        Some(("reset", sub_matches)) => {
            let req = if sub_matches.get_flag("hard") {
                cmd("reser -h")
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
                a.parse().unwrap()
            } else {
                0x08000000
            };
            let len = sub_matches
                .get_one::<String>("length")
                .map(|a| a.parse().unwrap());
            let filename = if let Some(a) = sub_matches.get_one::<String>("input") {
                a
            } else {
                unreachable!();
            };
            write_c(&mut port, std::fs::read(filename).unwrap(), addr, len);
        }

        None => unreachable!(),

        _ => (),
    }
}
