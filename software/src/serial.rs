use crate::ecc::calculate_crc;
use memmem::{Searcher, TwoWaySearcher};
use serialport::SerialPort;
use std::{io::Read, time::Duration};

use crate::command::SPIPAGESIZE;

pub type Port = Box<dyn SerialPort>;

pub fn connect_port(port_name: Option<&String>) -> Result<Port, serialport::Error> {
    let ports = serialport::available_ports().expect("No ports found!");
    let port = if let Some(a) = port_name {
        a
    } else {
        &ports.first().unwrap().port_name
    };
    println!("Port {} detected", port);
    Ok(serialport::new(port, 9600)
        .timeout(Duration::from_millis(500))
        .open()
        .expect("Failed to open port"))
}

pub fn communicate(port: &mut Port, input: &[u8]) -> Vec<u8> {
    write(port, input);
    let res = read_to_stars(port);
    let res_str = String::from_utf8(res.clone()).unwrap();
    if res_str.contains("rror") || res_str.contains("nvalid") || res_str.contains("rong") {
        panic!("Response:\n{}", res_str);
    };
    res
}

pub fn read_data(port: &mut Port, addr: u32, words: u32) -> Vec<u8> {
    let addr = addr / 4 * 4;
    write(port, format!("rd {addr} {words}\n").as_bytes());
    let len = words * 4;
    let data = read_len(port, len);
    let crc = u32::from_le_bytes(
        read_len(port, 4)
            .try_into()
            .expect("reading exactly 4 bytes"),
    );
    assert_eq!(b"Done".to_vec(), read_len(port, 4), "Exited unsuccesfull");
    assert_eq!(crc, calculate_crc(&data), "Corrupted data ");
    data
}

pub fn read_eflash_data(port: &mut Port, addr: u32, pages: u32) -> Vec<u8> {
    write(port, format!("spird {addr} {pages}\n").as_bytes());
    let len = pages * SPIPAGESIZE;
    let data = read_len(port, len);
    let crc = u32::from_le_bytes(
        read_len(port, 4)
            .try_into()
            .expect("reading exactly 4 bytes"),
    );
    assert_eq!(b"Done".to_vec(), read_len(port, 4), "Exited unsuccesfull");
    assert_eq!(crc, calculate_crc(&data), "Corrupted data ");
    data
}

pub fn write_chunk(port: &mut Port, chunk: &[u8]) {
    port.write_all(chunk).unwrap();
    let _dump = read_to_stars(port);
}

fn write(port: &mut Port, input: &[u8]) {
    port.clear(serialport::ClearBuffer::Output).unwrap();
    port.write_all(input).unwrap();
    port.flush().unwrap()
}

fn read_to_stars(port: &mut Port) -> Vec<u8> {
    let mut out: Vec<u8> = Vec::new();
    let searcher = TwoWaySearcher::new("***".as_bytes());
    for _vahtikoira in 0..200 {
        let mut serial_buf = [0u8; 256];
        if port.read(&mut serial_buf).is_err() {
            continue;
        };
        if let Some(pos) = searcher.search_in(&serial_buf) {
            out.extend_from_slice(serial_buf.split_at(pos).0);
            break;
        } else {
            out.extend_from_slice(&serial_buf);
        }
    }

    out
}

fn read_len(port: &mut Port, len: u32) -> Vec<u8> {
    let mut out = Vec::new();
    let mut bytes = port.bytes();
    for _ in 0..len {
        let last = bytes.next().map(|a| a.ok()).flatten();
        match last {
            Some(a) => out.push(a),
            None => {
                if out.len() > 20 {
                    //reasonable amount to output
                    panic!("Reading stopped prematurely")
                } else {
                    panic!(
                        "Reading failed. Response:\n{}",
                        String::from_utf8(out).unwrap()
                    );
                }
            }
        };
    }
    out
}
