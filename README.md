# EL334 Mini-Project: Dino Jump 🦖

เกมไดโนกระโดดบนบอร์ด **NXP FRDM-K64F** ร่วมกับ **Arduino Multi-function Shield**
รายวิชา EL334 Microprocessor and Microcontroller System Laboratory (ภาคเรียนที่ 1/2569)

| | |
|---|---|
| ผู้จัดทำ | นางสาว วโรชา เสาว์โร (1670900032), พุฒิพงศ์ บุตรรัตน์ (1670901899) |
| Section / Group | 336C /  Group 15  |

## วิธีเล่น
| ปุ่ม | หน้าเมนู | ขณะเล่น | หยุดชั่วคราว | จบเกม |
|---|---|---|---|---|
| S1 | เริ่มเกม | กระโดด (หลบกระบองเพชร `_`) | – | เล่นใหม่ |
| S2 | เปิด/ปิดเพลง | ก้ม (หลบนก `‾`) | – | – |
| S3 | – | 1 ครั้ง = หยุด, 2 ครั้ง = เมนู | 1 ครั้ง = เล่นต่อ, 2 ครั้ง = เมนู | กลับเมนู |

- หมุน Potentiometer ในหน้าเมนูเพื่อเลือกระดับ `LEV1`–`LEV3`
- LED D1–D3 = จำนวนชีวิต, D4 กะพริบเมื่อได้คะแนน

## สิ่งที่ใช้
- **Timer:** PIT ch0 (ทุก 1 ms: ฐานเวลา, สแกน 7-Segment, จังหวะเพลง), PIT ch1 (สร้างเสียง PWM)
- **Interrupt:** PIT0, PIT1, PORTB (ปุ่ม S1–S3, falling edge + debounce 150 ms)
- **ADC0:** อ่าน Potentiometer (12-bit) เลือกระดับความยาก
- **GPIO:** LED, 74HC595 (7-Segment), Buzzer

## โครงสร้างไฟล์
```
src/main.c                    โปรแกรมเกม (ภาษา C, register-level, MK64F12.h)
test/test_board_silent.c      โปรแกรมทดสอบอุปกรณ์บน Shield
docs/                         รายงาน (Word) และ Flowchart / Diagram
```

## วิธีคอมไพล์ (Keil µVision)
1. สร้างโปรเจกต์สำหรับ MK64FN1M0VLL12 แล้วนำ `src/main.c` ไปแทน main.c เดิม
2. Options for Target → Debug และ Utilities → เลือก **CMSIS-DAP Debugger**
3. Build (F7) แล้ว Download (F8)

## เพลงที่ใช้
- หน้าเมนู: *The Entertainer* – Scott Joplin (1902, public domain)
- เพลงอื่นในเกมแต่งขึ้นเองโดยกลุ่มผู้จัดทำ
