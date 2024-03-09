use std::{cmp, collections::HashMap};

use crate::{
    ecc::calculate_crc,
    serial::{communicate, read_data, write_chunk, Port},
};

pub fn connect(port: &mut Port) -> Vec<u8> {
    communicate(port, &cmd("connect"))
}

pub fn erase(port: &mut Port, addr: u32, pages: Option<u32>) {
    let info_str = String::from_utf8(connect(port)).unwrap();
    let info = chop(&info_str);
    let flash_size: u32 = info.get("FlashSize").unwrap().parse().unwrap();
    let page_size: u32 = info.get("PageSize").unwrap().parse().unwrap();
    let max_pages = flash_size / page_size;
    let page_count = if let Some(p) = pages { p } else { max_pages };
    assert!(
        page_count <= max_pages,
        "Impossible to erase {page_count} pages. Maximum {max_pages}"
    );

    let _loader_res = communicate(port, &cmd("loader"));
    let buf_size_str = communicate(port, &cmd("bufsize"));
    //how did you even come up with this? You had a nice map!
    let buf_size_words = buf_size_str
        .strip_prefix(b"MaxBuf = ")
        .expect("Failed to get buffer size");
    let buf_size_words: u32 = String::from_utf8(buf_size_words[..4].to_vec())
        .unwrap()
        .parse()
        .unwrap();
    let _buf_size_bytes = buf_size_words * 4;

    println!("Erasing...");
    let _ = communicate(port, &cmd(&format!("erase {addr} {page_count}")));
    println!("Erasing done.");
}

pub fn read_crc(port: &mut Port, addr: u32, len: Option<u32>) -> u32 {
    let info_str = String::from_utf8(connect(port)).unwrap();
    let info = chop(&info_str);
    let flash_size = info.get("FlashSize").unwrap().parse().unwrap();
    let len = match len {
        Some(a) => a,
        None => flash_size,
    };
    let payload_words = len.div_ceil(4);
    let crc_str = communicate(port, &cmd(&format!("crc {} {}", addr, payload_words)));
    let crc_str = String::from_utf8(
        crc_str
            .strip_prefix(b"Crc32 = 0x")
            .expect("Failed to get CRC")
            .to_vec(),
    )
    .unwrap();
    u32::from_be_bytes(hex::decode(crc_str.trim()).unwrap().try_into().unwrap())
}

pub fn read_c(port: &mut Port, addr: u32, len: Option<u32>) -> Vec<u8> {
    let info_str = String::from_utf8(connect(port)).unwrap();
    let info = chop(&info_str);
    let flash_size = info.get("FlashSize").unwrap().parse().unwrap();
    let len = match len {
        Some(a) => a,
        None => flash_size,
    };
    let payload_words = len.div_ceil(4);
    println!("Reading {payload_words} words from address 0x{:08x}", addr);
    read_data(port, addr, payload_words)
}

pub fn write_c(port: &mut Port, mut data: Vec<u8>, addr: u32, len: Option<u32>) {
    let len = if let Some(len) = len {
        cmp::min(data.len() as u32, len)
    } else {
        data.len() as u32
    };
    let payload_words = len.div_ceil(4);
    data.resize(payload_words as usize * 4, 0);

    let info_str = String::from_utf8(connect(port)).unwrap();
    let info = chop(&info_str);
    let flash_size: u32 = info.get("FlashSize").unwrap().parse().unwrap();
    assert!(
        flash_size >= len,
        "Firmware would not fit. Available {flash_size} bytes, needed {len} bytes."
    );
    let page_size: u32 = info.get("PageSize").unwrap().parse().unwrap();
    let page_count = len.div_ceil(page_size);

    let _loader_res = communicate(port, &cmd("loader"));
    let buf_size_str = communicate(port, &cmd("bufsize"));
    //how did you even come up with this? You had a nice map!
    let buf_size_words = buf_size_str
        .strip_prefix(b"MaxBuf = ")
        .expect("Failed to get buffer size");
    let buf_size_words: u32 = String::from_utf8(buf_size_words[..4].to_vec())
        .unwrap()
        .parse()
        .unwrap();
    let buf_size_bytes = buf_size_words * 4;
    let _parts_count = len.div_ceil(buf_size_bytes);

    println!("Erasing...");
    let _ = communicate(port, &cmd(&format!("erase {addr} {page_count}")));
    println!("Erasing done, flashing...");

    for (i, chunk) in data.chunks(buf_size_bytes as usize).enumerate() {
        println!(
            "Writing {} bytes at 0x{:08x}",
            chunk.len(),
            addr + (i as u32) * buf_size_bytes
        );
        let r = communicate(port, &cmd(&format!("loadbuffer {}", chunk.len() / 4)));
        r.strip_prefix(b"Load ready")
            .expect("loadbuffer - operation failed");

        write_chunk(port, chunk);
        let c = format!(
            "writebuffer {} {}",
            addr + (i as u32) * buf_size_bytes,
            chunk.len() / 4
        );
        let _ = communicate(port, &cmd(&c));
    }

    assert_eq!(read_crc(port, addr, Some(len)), calculate_crc(&data));
    println!("Flash written and verified. Have fun!");

    simple_command(port, &cmd("reset"));
}

pub fn simple_command(port: &mut Port, command: &[u8]) -> Vec<u8> {
    let _ = connect(port);
    communicate(port, command)
}

pub fn cmd(name: &str) -> Vec<u8> {
    format!("\n{name}\n").as_bytes().to_owned()
}

fn chop(input: &str) -> HashMap<&str, &str> {
    let mut out = HashMap::new();
    for pair in input.split('\n') {
        if let Some((key, value)) = pair.trim().trim_matches('\0').split_once(' ') {
            out.insert(key, value);
        }
    }
    out
}
