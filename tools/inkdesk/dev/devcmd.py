# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The InkDesk Authors
import serial,time,glob,sys
p=sorted(glob.glob('/dev/cu.usbmodem*'))[0]
s=serial.Serial(p,115200,timeout=0.3); s.reset_input_buffer()
s.write(("CMD:"+sys.argv[1]+"\n").encode()); end=time.time()+float(sys.argv[2] if len(sys.argv)>2 else 3); buf=b''
while time.time()<end: buf+=s.read(max(1,s.in_waiting))
print("\n".join(l for l in buf.decode('utf-8','replace').splitlines() if any(k in l for k in ("_OK","ERR","SDFS","I18N","ERR]"))))
