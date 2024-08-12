use std::{cmp, collections::HashMap};

use crate::{
    ecc::calculate_crc,
    serial::{communicate, read_data, read_eflash_data, write_chunk, Port},
};

type InfoMap = HashMap<String, String>;

pub const SPIPAGESIZE: u32 = 256;
pub const FLASH_USERDATA_ADDRESS: u32 = 0x08000000;
const SPIERASELIMIT: usize = 256;

pub fn connect(port: &mut Port) -> Result<InfoMap, String> {
    let res = communicate(port, &cmd("connect"));
    let res_str = String::from_utf8(res).unwrap();
    let res_map = chop(&res_str);
    if res_map.get("Target").is_none() {
        return Err(format!("Connection unsuccessful\n{}", res_str));
    }
    Ok(res_map)
}

pub fn erase(port: &mut Port, info: InfoMap, _loader_info: InfoMap, addr: u32, pages: Option<u32>) {
    let flash_size: u32 = info.get("FlashSize").unwrap().parse().unwrap();
    let max_address = flash_size + FLASH_USERDATA_ADDRESS;
    assert!(
        addr < max_address,
        "Impossible to erase addr 0x{:08X}. Maximum 0x{:08X}",
        addr,
        max_address - 1
    );
    assert!(
        addr >= FLASH_USERDATA_ADDRESS,
        "Impossible to erase addr 0x{:08X}. Minimum 0x{:08X}",
        addr,
        FLASH_USERDATA_ADDRESS
    );
    let page_size: u32 = info.get("PageSize").unwrap().parse().unwrap();
    let max_pages = flash_size / page_size;
    let addr_page = (addr - FLASH_USERDATA_ADDRESS) / page_size;
    let page_count = if let Some(p) = pages {
        p
    } else {
        max_pages - addr_page
    };
    let addr = addr / page_size * page_size;
    let required_page_count = max_pages - addr_page;
    assert!(
        page_count <= required_page_count,
        "Impossible to erase {page_count} pages. Maximum {required_page_count}"
    );
    let o = communicate(port, &cmd(&format!("erase {addr} {page_count}")));
    println!("{}", String::from_utf8(o.to_vec()).unwrap());
}

pub fn erase_eflash(port: &mut Port, loader_info: InfoMap, addr: u32, pages: Option<u32>) {
    let flash_size: u32 = loader_info.get("SPIFlashSize").unwrap().parse().unwrap();
    assert!(
        addr < flash_size,
        "Impossible to erase from addr 0x{:08X}. Maximum 0x{:08X}",
        addr,
        flash_size - 1
    );

    let max_pages = flash_size / SPIPAGESIZE;
    let addr_page = addr / SPIPAGESIZE;
    let page_count = if let Some(p) = pages {
        p
    } else {
        max_pages - addr_page
    };
    let addr = addr_page * SPIPAGESIZE;
    let required_page_count = max_pages - addr_page;
    assert!(
        page_count <= required_page_count,
        "Impossible to erase {page_count} pages. Maximum {required_page_count}"
    );

    println!(
        "Erasing eflash {} pages from address 0x{:08x?}...",
        page_count, addr
    );
    for (i, block_addr) in (addr..(addr + page_count * SPIPAGESIZE))
        .step_by(SPIERASELIMIT * SPIPAGESIZE as usize)
        .enumerate()
    {
        let block_size = cmp::min(
            page_count - (i * SPIERASELIMIT) as u32,
            SPIERASELIMIT as u32,
        );
        let o = communicate(port, &cmd(&format!("spierase {block_addr} {block_size}")));
        println!("{}", String::from_utf8(o.to_vec()).unwrap());
    }
}

pub fn read_crc(port: &mut Port, info: InfoMap, addr: u32, len: Option<u32>) {
    let flash_size: u32 = info.get("FlashSize").unwrap().parse().unwrap();
    let max_address = flash_size + FLASH_USERDATA_ADDRESS;
    assert!(
        addr < max_address,
        "Impossible to crc from addr 0x{:08X}. Maximum 0x{:08X}",
        addr,
        max_address - 1
    );
    assert!(
        addr >= FLASH_USERDATA_ADDRESS,
        "Impossible to crc from addr 0x{:08X}. Minimum 0x{:08X}",
        addr,
        FLASH_USERDATA_ADDRESS
    );

    let max_words = flash_size / 4;
    let addr_words = (addr - FLASH_USERDATA_ADDRESS) / 4;
    let (len, len_text) = if let Some(l) = len {
        let len = ((addr + l).div_ceil(4) - addr / 4) * 4;
        let required_words = max_words - addr_words;
        assert!(
            len / 4 <= required_words,
            "Impossible to crc {} bytes. Maximum {}",
            len,
            required_words
        );
        (len, format!("CRC of {} bytes", len))
    } else {
        (
            (max_words - addr_words) * 4,
            "CRC of available memory".to_string(),
        )
    };
    let addr = addr / 4 * 4;
    println!(
        "{} starting from 0x{:08X} is 0x{:08X}",
        len_text,
        addr,
        crc(port, addr, len)
    );
}

pub fn read_c(port: &mut Port, info: InfoMap, addr: u32, len: Option<u32>) -> Vec<u8> {
    let flash_size: u32 = info.get("FlashSize").unwrap().parse().unwrap();
    let max_address = flash_size + FLASH_USERDATA_ADDRESS;
    assert!(
        addr < max_address,
        "Impossible to read from addr 0x{:08X}. Maximum 0x{:08X}",
        addr,
        max_address - 1
    );
    assert!(
        addr >= FLASH_USERDATA_ADDRESS,
        "Impossible to read from addr 0x{:08X}. Minimum 0x{:08X}",
        addr,
        FLASH_USERDATA_ADDRESS
    );
    let len = match len {
        Some(a) => a,
        None => flash_size - (addr - FLASH_USERDATA_ADDRESS),
    };
    let words = (addr + len).div_ceil(4) - addr / 4;
    let file_addr = addr % 4;
    let addr = addr / 4 * 4;

    let max_words = flash_size / 4;
    let addr_words = (addr - FLASH_USERDATA_ADDRESS) / 4;
    let required_words = max_words - addr_words;
    assert!(
        words <= required_words,
        "Impossible to read {} bytes. Maximum {}",
        words * 4,
        required_words * 4
    );
    println!("Reading {words} words from address 0x{:08x}", addr);

    let data =
        read_data(port, addr, words)[(file_addr as usize)..((file_addr + len) as usize)].to_vec();
    println!("Read Done");
    data
}

pub fn read_eflash(port: &mut Port, spi_info: InfoMap, addr: u32, len: Option<u32>) -> Vec<u8> {
    let flash_size = spi_info.get("SPIFlashSize").unwrap().parse().unwrap();
    assert!(
        addr < flash_size,
        "Impossible to read from addr 0x{:08X}. Maximum 0x{:08X}",
        addr,
        flash_size - 1
    );
    let len = match len {
        Some(a) => a,
        None => flash_size - addr,
    };
    let max_pages = flash_size / SPIPAGESIZE;
    let addr_page = addr / SPIPAGESIZE;
    let page_count = (addr + len).div_ceil(SPIPAGESIZE) - addr_page;
    let file_addr = addr % SPIPAGESIZE;
    let addr = addr_page * SPIPAGESIZE;

    let required_page_count = max_pages - addr_page;
    assert!(
        page_count <= required_page_count,
        "Impossible to read {page_count} pages. Maximum {required_page_count}"
    );
    println!("Reading {page_count} pages from address 0x{:08x}", addr);
    let data = read_eflash_data(port, addr, page_count)
        [(file_addr as usize)..((file_addr + len) as usize)]
        .to_vec();
    println!("Read Done");
    data
}

pub fn write_c(
    port: &mut Port,
    info: InfoMap,
    loader_info: InfoMap,
    mut data: Vec<u8>,
    addr: u32,
    len: Option<u32>,
) {
    let len = if let Some(len) = len {
        len
    } else {
        data.len() as u32
    };
    data.resize(len as usize, 0);
    let page_size: u32 = info.get("PageSize").unwrap().parse().unwrap();
    let flash_size: u32 = info.get("FlashSize").unwrap().parse().unwrap();

    let max_address = flash_size + FLASH_USERDATA_ADDRESS;
    assert!(
        addr < max_address,
        "Impossible to write to addr 0x{:08X}. Maximum 0x{:08X}",
        addr,
        max_address - 1
    );
    assert!(
        addr >= FLASH_USERDATA_ADDRESS,
        "Impossible to write to addr 0x{:08X}. Minimum 0x{:08X}",
        addr,
        FLASH_USERDATA_ADDRESS
    );
    let addr_aligned = addr / page_size * page_size;
    let page_count = (len + addr - addr_aligned).div_ceil(page_size);
    let required_size = page_count * page_size + (addr_aligned - FLASH_USERDATA_ADDRESS);
    assert!(
        flash_size >= required_size,
        "Firmware would not fit. Available {flash_size} bytes, required {required_size} bytes."
    );

    if addr != addr_aligned {
        let first_page = read_data(port, addr_aligned, page_size / 4);
        communicate(port, &cmd("loader")); // reset after read
        let adjunct_len = (addr - addr_aligned) as usize;
        data = [&first_page[..adjunct_len], &data].concat();
    }

    if (len + addr - addr_aligned) != page_count * page_size {
        let last_page = read_data(
            port,
            addr_aligned + (page_count - 1) * page_size,
            page_size / 4,
        );
        communicate(port, &cmd("loader")); // reset after read
        let apendix_addr = ((len + addr - addr_aligned) - (page_count - 1) * page_size) as usize;
        data = [&data, &last_page[apendix_addr..]].concat();
    };

    erase(
        port,
        info,
        loader_info.clone(),
        addr_aligned,
        Some(page_count),
    );

    let buf_size_bytes: u32 = loader_info.get("MaxBufSize").unwrap().parse().unwrap();

    for (i, chunk) in data.chunks(buf_size_bytes as usize).enumerate() {
        println!(
            "Writing {} bytes at 0x{:08x}",
            chunk.len(),
            addr_aligned + (i as u32) * buf_size_bytes
        );
        let r = communicate(port, &cmd(&format!("loadbuffer {}", chunk.len() / 4)));
        r.strip_prefix(b"Load ready")
            .expect("loadbuffer - operation failed");

        write_chunk(port, chunk);
        let c = format!(
            "writebuffer {} {}",
            addr_aligned + (i as u32) * buf_size_bytes,
            chunk.len() / 4
        );
        let o = communicate(port, &cmd(&c));
        assert!(o == b"Write OK\r\n", "{}", String::from_utf8(o).unwrap());
    }

    assert_eq!(
        crc(port, addr_aligned, page_count * page_size),
        calculate_crc(&data),
        "Verification failed"
    );
    println!("Flash written and verified. Have fun!");

    simple_command(port, &cmd("reset"));
}

pub fn write_eflash(
    port: &mut Port,
    loader_info: InfoMap,
    mut data: Vec<u8>,
    addr: u32,
    len: Option<u32>,
) {
    let len = if let Some(len) = len {
        len
    } else {
        data.len() as u32
    };
    data.resize(len as usize, 0);
    let spi_flash_size: u32 = loader_info.get("SPIFlashSize").unwrap().parse().unwrap();
    assert!(
        addr < spi_flash_size,
        "Impossible to write to addr 0x{:08X}. Maximum 0x{:08X}",
        addr,
        spi_flash_size - 1
    );
    let addr_aligned = addr / SPIPAGESIZE * SPIPAGESIZE;
    let page_count = (len + addr - addr_aligned).div_ceil(SPIPAGESIZE);
    let required_size = page_count * SPIPAGESIZE + addr_aligned;
    assert!(
        spi_flash_size >= required_size,
        "Image would not fit. Available {spi_flash_size} bytes, required {required_size} bytes."
    );

    if addr != addr_aligned {
        let first_page = read_eflash_data(port, addr_aligned, 1);
        communicate(port, &cmd("loader")); // reset after read
        let adjunct_len = (addr - addr_aligned) as usize;
        data = [&first_page[..adjunct_len], &data].concat();
    }

    if (len + addr - addr_aligned) != page_count * SPIPAGESIZE {
        let last_page = read_eflash_data(port, addr_aligned + (page_count - 1) * SPIPAGESIZE, 1);
        communicate(port, &cmd("loader")); // reset after read
        let apendix_addr = ((len + addr - addr_aligned) - (page_count - 1) * SPIPAGESIZE) as usize;
        data = [&data, &last_page[apendix_addr..]].concat();
    };

    erase_eflash(port, loader_info.clone(), addr_aligned, Some(page_count));

    let buf_size_bytes: u32 = loader_info.get("MaxBufSize").unwrap().parse().unwrap();

    for (i, chunk) in data.chunks(buf_size_bytes as usize).enumerate() {
        println!(
            "Writing {} bytes at 0x{:08x}",
            chunk.len(),
            addr_aligned + (i as u32) * buf_size_bytes
        );
        let r = communicate(port, &cmd(&format!("loadbuffer {}", chunk.len() / 4)));
        r.strip_prefix(b"Load ready")
            .expect("loadbuffer - operation failed");

        write_chunk(port, chunk);
        let c = format!(
            "spiwritebuffer {} {}",
            addr_aligned + (i as u32) * buf_size_bytes,
            chunk.len() as u32 / SPIPAGESIZE
        );
        let o = communicate(port, &cmd(&c));
        assert!(o == b"Write OK\r\n", "{}", String::from_utf8(o).unwrap());
    }

    println!("Image written.");
}

pub fn get_loader_info(port: &mut Port) -> Result<InfoMap, String> {
    let _loader_res = communicate(port, &cmd("loader"));
    let loader_info_str = String::from_utf8(communicate(port, &cmd("loader"))).unwrap();
    let loader_info = chop(&loader_info_str);
    let loader_status = loader_info.get("Loader");
    if loader_status.is_none() || loader_status.unwrap() == "error" {
        return Err(loader_info_str);
    };
    Ok(loader_info)
}

pub fn get_buf_size(port: &mut Port) -> u32 {
    let buf_size_str = communicate(port, &cmd("bufsize"));
    let buf_size_words = buf_size_str
        .strip_prefix(b"MaxBuf = ")
        .expect("Failed to get buffer size");
    let buf_size_words: u32 = String::from_utf8(buf_size_words[..4].to_vec())
        .unwrap()
        .parse()
        .unwrap();
    let buf_size_bytes: u32 = buf_size_words * 4;
    buf_size_bytes
}

fn crc(port: &mut Port, addr: u32, len: u32) -> u32 {
    let payload_words = (addr + len).div_ceil(4) - addr / 4;
    let addr = addr / 4 * 4;
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

pub fn simple_command(port: &mut Port, command: &[u8]) -> Vec<u8> {
    let _ = connect(port);
    communicate(port, command)
}

pub fn cmd(name: &str) -> Vec<u8> {
    format!("\n{name}\n").as_bytes().to_owned()
}

fn chop(input: &str) -> InfoMap {
    let mut out = HashMap::new();
    for pair in input.split('\n') {
        if let Some((key, value)) = pair.trim().trim_matches('\0').split_once(' ') {
            out.insert(key.to_owned(), value.to_owned());
        }
    }
    out
}
