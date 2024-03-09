use crate::ecc::calculate_crc;
use memmem::{Searcher, TwoWaySearcher};
use serialport::{ClearBuffer, SerialPort};
use std::{io::Read, time::Duration};

pub type Port = Box<dyn SerialPort>;

pub fn connect_port(port_name: Option<&String>) -> Result<Port, serialport::Error> {
    let ports = serialport::available_ports().expect("No ports found!");
    let port = if let Some(a) = port_name {
        a
    } else {
        &ports.get(0).unwrap().port_name
    };
    println!("{}", port);
    Ok(serialport::new(port, 9600)
        .timeout(Duration::from_millis(500))
        .open()
        .expect("Failed to open port"))
}

pub fn communicate(port: &mut Port, input: &[u8]) -> Vec<u8> {
    write(port, input).unwrap();
    read_to_stars(port).unwrap()
}

pub fn read_data(port: &mut Port, addr: u32, words: u32) -> Vec<u8> {
    write(port, format!("rd {addr} {words}\n").as_bytes()).unwrap();
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
    port.write(chunk).unwrap();
    let _dump = read_to_stars(port).unwrap();
}

fn write(port: &mut Port, input: &[u8]) -> Result<(), Error> {
    port.clear(serialport::ClearBuffer::Output)
        .or(Err(Error::ClearFailed))?;
    port.write(input).or(Err(Error::WriteFailed))?;
    port.flush().or(Err(Error::FlushFailed))
}

fn read_to_stars(port: &mut Port) -> Result<Vec<u8>, Error> {
    let mut out = Vec::new();
    let searcher = TwoWaySearcher::new("***".as_bytes());
    for vahtikoira in 0..100 {
        let mut serial_buf = [0u8; 256];
        if port.read(&mut serial_buf).is_err() {
            continue;
        };
        if let Some(pos) = searcher.search_in(&serial_buf) {
            out.extend_from_slice(&serial_buf.split_at(pos).0.to_owned());
            break;
        } else {
            out.extend_from_slice(&serial_buf);
        }
    }

    Ok(out)
}

fn read_len(port: &mut Port, len: u32) -> Vec<u8> {
    let mut out = Vec::new();
    let mut bytes = port.bytes();
    for _ in 0..len {
        out.push(bytes.next().unwrap().unwrap());
    }
    out
}

#[derive(Debug)]
enum Error {
    ClearFailed,
    FlushFailed,
    NoDataToRead,
    WriteFailed,
}
