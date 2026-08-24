import sys
import os

def read_varlen(data, pos):
    val = 0
    while pos < len(data):
        b = data[pos]
        pos += 1
        val = (val << 7) | (b & 0x7F)
        if not (b & 0x80):
            break
    return val, pos

def write_varlen(val):
    buf = []
    buf.append(val & 0x7F)
    val >>= 7
    while val > 0:
        buf.append((val & 0x7F) | 0x80)
        val >>= 7
    buf.reverse()
    return bytes(buf)

def fix_roland_sysex(sdata):
    arr = bytearray(sdata)
    offset = 1 if (len(arr) > 0 and arr[0] == 0xF0) else 0

    # Roland DT1 (0x12) Dataset 명령 패킷 검사: 41 <dev> <model> 12 <addr...> <data...> <chk> F7
    if len(arr) >= offset + 6:
        if arr[offset] == 0x41 and arr[offset+3] == 0x12:
            end_idx = len(arr)
            if arr[-1] == 0xF7:
                end_idx -= 1
            
            addr_data_start = offset + 4
            chk_idx = end_idx - 1

            if chk_idx > addr_data_start:
                sum_val = sum(arr[addr_data_start:chk_idx]) % 128
                correct_chk = (128 - sum_val) & 0x7F
                old_chk = arr[chk_idx]

                if old_chk != correct_chk:
                    print(f"  [체크섬 수정] 기존: 0x{old_chk:02X} -> 정산값: 0x{correct_chk:02X}")
                    arr[chk_idx] = correct_chk
    return bytes(arr)

def process_midi_file(in_path, out_path):
    print(f"\n처리 중: {in_path}")
    with open(in_path, "rb") as f:
        data = f.read()

    if data[:4] != b"MThd":
        print("올바른 MIDI 파일이 아닙니다.")
        return

    header_len = int.from_bytes(data[4:8], "big")
    tracks_count = int.from_bytes(data[10:12], "big")
    division = data[12:14]

    out_data = bytearray()
    out_data.extend(b"MThd")
    out_data.extend(int(6).to_bytes(4, "big"))
    out_data.extend(data[8:10]) # Format
    out_data.extend(tracks_count.to_bytes(2, "big"))
    out_data.extend(division)

    pos = 8 + header_len
    fixed_count = 0

    for t in range(tracks_count):
        if pos >= len(data) or data[pos:pos+4] != b"MTrk":
            break
        track_len = int.from_bytes(data[pos+4:pos+8], "big")
        track_data = data[pos+8:pos+8+track_len]
        pos += 8 + track_len

        t_pos = 0
        t_len = len(track_data)
        out_track = bytearray()
        running_status = None

        while t_pos < t_len:
            delta, t_pos = read_varlen(track_data, t_pos)
            out_track.extend(write_varlen(delta))
            if t_pos >= t_len:
                break

            b = track_data[t_pos]
            if b & 0x80:
                t_pos += 1
                status = b
                running_status = status if status < 0xF0 else None
            else:
                status = running_status

            if status == 0xFF: # Meta Event
                meta_type = track_data[t_pos]
                t_pos += 1
                mlen, t_pos = read_varlen(track_data, t_pos)
                mdata = track_data[t_pos:t_pos+mlen]
                t_pos += mlen

                out_track.append(0xFF)
                out_track.append(meta_type)
                out_track.extend(write_varlen(mlen))
                out_track.extend(mdata)
            elif status in (0xF0, 0xF7): # SysEx Event
                slen, t_pos = read_varlen(track_data, t_pos)
                sdata = track_data[t_pos:t_pos+slen]
                t_pos += slen

                fixed_sdata = fix_roland_sysex(sdata)
                out_track.append(status)
                out_track.extend(write_varlen(len(fixed_sdata)))
                out_track.extend(fixed_sdata)
            elif (status & 0xF0) in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
                d1 = track_data[t_pos]
                d2 = track_data[t_pos+1]
                t_pos += 2
                out_track.append(status)
                out_track.append(d1)
                out_track.append(d2)
            elif (status & 0xF0) in (0xC0, 0xD0):
                d1 = track_data[t_pos]
                t_pos += 1
                out_track.append(status)
                out_track.append(d1)
            else:
                out_track.append(status)

        out_data.extend(b"MTrk")
        out_data.extend(len(out_track).to_bytes(4, "big"))
        out_data.extend(out_track)

    with open(out_path, "wb") as f:
        f.write(out_data)
    print(f"완료 -> 저장 파일: {out_path}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("사용법: python fix_roland_checksum.py <파일1.mid> [파일2.mid ...]")
    else:
        for p in sys.argv[1:]:
            base, ext = os.path.splitext(p)
            out_p = f"{base}_fixed{ext}"
            process_midi_file(p, out_p)