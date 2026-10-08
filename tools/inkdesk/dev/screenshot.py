# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The InkDesk Authors
import serial,time,glob,sys
from PIL import Image
p=sorted(glob.glob('/dev/cu.usbmodem*'))[0]
s=serial.Serial(p,115200,timeout=2); s.reset_input_buffer()
s.write(b"CMD:SCREENSHOT\n"); buf=b''; end=time.time()+15
while time.time()<end and b'SCREENSHOT_END' not in buf: buf+=s.read(8192)
i=buf.index(b'SCREENSHOT_START:'); j=buf.index(b'\n',i); n=int(buf[i+17:j]); data=buf[j+1:j+1+n]
img=Image.frombytes('1',(800,480),data)  # panel-native landscape
img=img.rotate(-90,expand=True) if len(sys.argv)<3 else img
img.save(sys.argv[1] if len(sys.argv)>1 else 'screen.png'); print('saved',n)
