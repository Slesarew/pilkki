use crc::{CRC_32_ISO_HDLC, Crc};

pub fn calculate_crc(data: &[u8]) -> u32 {
    let crc = Crc::<u32>::new(&CRC_32_ISO_HDLC);
    let mut digest = crc.digest();
    digest.update(data);
    digest.finalize()
}

