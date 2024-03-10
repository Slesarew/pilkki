use crate::ecc::calculate_crc;
use memmem::{Searcher, TwoWaySearcher};
use serialport::SerialPort;
use std::{io::Read, time::Duration};

pub type Port = Box<dyn SerialPort>;

pub fn connect_port(port_name: Option<&String>) -> Result<Port, serialport::Error> {
    let ports = serialport::available_ports().expect("No ports found!");
    let port = if let Some(a) = port_name {
        a
    } else {
        &ports.first().unwrap().port_name
    };
    println!("{}", port);
    Ok(serialport::new(port, 9600)
        .timeout(Duration::from_millis(500))
        .open()
        .expect("Failed to open port"))
}

pub fn communicate(port: &mut Port, input: &[u8]) -> Vec<u8> {
    write(port, input);
    read_to_stars(port)
}

pub fn read_data(port: &mut Port, addr: u32, words: u32) -> Vec<u8> {
    write(port, format!("rd {addr} {words}\n").as_bytes());
    let len = words * 4;
    let data = read_len(port, len);
    let crc = u32::from_le_bytes(
        read_len(port, 4)
            .try_into()
            .expect("reading exactly 4 bytes"),
    );
    assert_eq!(b"Done".to_vec(), read_len(port, 4));
    assert_eq!(crc, calculate_crc(&data));
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
    let mut out = Vec::new();
    let searcher = TwoWaySearcher::new("***".as_bytes());
    for _vahtikoira in 0..100 {
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
        out.push(bytes.next().unwrap().unwrap());
    }
    out
}
