"""Render an original signal-flow illustration (not a UI screenshot). Requires Pillow."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import argparse
p=argparse.ArgumentParser(); p.add_argument('--font',required=True); args=p.parse_args()
im=Image.new('RGB',(1600,800),'#101217'); d=ImageDraw.Draw(im)
red='#ff164e'; white='#f4f5f6'; muted='#aab0bc'
def text(x,y,s,size,color=white):
 d.text((x,y),s,font=ImageFont.truetype(args.font,size),fill=color)
d.rectangle((0,0,1600,9),fill=red)
text(70,55,'MAC DIGI / RARE DATA',24,red)
text(70,110,'FORCE LINK AUDIO',76)
text(74,215,'Stereo main mix. Over your network.',30,muted)
for x,w in [(70,410),(610,380),(1120,410)]:
 d.rounded_rectangle((x,335,x+w,555),18,fill='#1b202a',outline='#424957',width=2)
text(103,365,'FORCE',38)
text(103,433,'Standalone',26,muted)
text(103,477,'Pads + controls active',22,muted)
text(645,365,'TRUSTED LAN',34)
text(645,433,'Link Audio',28)
text(645,477,'Wi-Fi / Ethernet',22,muted)
text(1155,365,'RECEIVER',36)
text(1155,433,'Link Audio host',24)
text(1155,477,'Stereo audio input',22,muted)
for x in [495,1005]:
 d.line((x,445,x+95,445),fill=red,width=6)
 d.polygon([(x+95,445),(x+74,432),(x+74,458)],fill=red)
text(74,625,'NETWORK PUBLISHING OFF UNTIL ENABLED',24,red)
text(74,681,'Built while connecting Force with COMPA.',25,muted)
text(74,742,'SIGNAL FLOW / v1.1.0',18,muted)
out=Path(__file__).resolve().parents[1]/'docs/images/force-link-audio-card.png'
im.save(out,optimize=True)
print(out)
