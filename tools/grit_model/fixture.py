"""Independent double-precision Python/scipy reference for the C++ lookup-table port."""
from pathlib import Path
import json,struct
import numpy as np
from scipy.signal import lfilter
root=Path(__file__).resolve().parents[2]
p=json.loads(Path(__file__).with_name('fit.json').read_text())
driven=json.loads(Path(__file__).with_name('driven_fit.json').read_text())
sr=48000;n=32768;t=np.arange(n)/sr
x=.22*np.sin(2*np.pi*43*t)+.11*np.sin(2*np.pi*997*t)+.04*np.random.default_rng(501).standard_normal(n)
x[:1024]=0;x[1024]=.9;x[16384:17408]=0

def lowpass(x,fc):
 k=np.tan(np.pi*fc/sr);b=k/(1+k)
 return lfilter([b,b],[1,(k-1)/(k+1)],x)
calibration=json.loads(Path(__file__).with_name("calibration.json").read_text())
def render(knob):
 knob=np.interp(knob,np.linspace(0,18,len(calibration["reference_knob"])),calibration["reference_knob"])
 push=10**(np.interp(knob,driven['knob_db'],driven['makeup_db'])/20);z=x*push;y=p['direct']*z
 for hz,weights in zip(p['poles'],p['weights']):
  v=weights[0]*z
  for th,w in zip(p['thresholds'],weights[1:]):v+=w*th*np.tanh(z/th)
  y+=lowpass(v,hz)
 for hz,w in zip(p['core_frequencies'],p['core_weights']):
  v=lowpass(z,hz)*np.sqrt(1+(40/hz)**2);v/=np.sqrt(1+v*v);y+=w*v**3
 y=y/(1+(abs(y)/driven['threshold'])**driven['knee'])**(1/driven['knee'])
 y-=lowpass(y,1)
 return x+(y/push-x)*min(1,knob/3)
with (root/'tests/data/grit_lab_fixture.bin').open('wb') as f:
 f.write(struct.pack('<II',n,sr))
 for v in [x,render(6),render(18)]:f.write(v.astype('<f8').tobytes())
