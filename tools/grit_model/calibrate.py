"""Generate a perceptual taper and static loudness compensation from the fitted C++ path.
Requires numpy, scipy, pyloudnorm and a C++17 compiler. Writes only generated calibration
JSON here; the temporary rendering library is built in the system temporary directory.
"""
from pathlib import Path
import ctypes, importlib.util, json, subprocess, tempfile
import numpy as np
import pyloudnorm as pyln
from scipy.signal import coherence, welch
root=Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix='bqst-grit-cal-') as temp:
 temp=Path(temp)
 (temp/'render.cpp').write_text('#include "BqtGritLabModel.h"\nextern "C" void render(const double* x,double* y,int n,double sr,double knob){bqt::GritLabModel m;m.prepare(sr);m.setReferenceKnob(knob);for(int i=0;i<n;++i)y[i]=m.process(x[i]);}\n')
 subprocess.run(['c++','-std=c++17','-O3','-shared','-fPIC','-I'+str(root/'src'),str(temp/'render.cpp'),'-o',str(temp/'render.so')],check=True)
 lib=ctypes.CDLL(str(temp/'render.so'));fn=lib.render
 ptr=np.ctypeslib.ndpointer(dtype=np.float64,flags='C_CONTIGUOUS');fn.argtypes=[ptr,ptr,ctypes.c_int,ctypes.c_double,ctypes.c_double]
 def render(x,k):
  x=np.ascontiguousarray(x,dtype='float64');y=np.empty_like(x);fn(x,y,len(x),48000,k);return y
 spec=importlib.util.spec_from_file_location('cal',root/'tools/calibrate_autogain.py');cal=importlib.util.module_from_spec(spec);spec.loader.exec_module(cal)
 mats={n:np.asarray(x) for n,x in cal.materials.items()}
 # Same nonlinear-energy metric and -40 dB / knob 1 anchor as Cream.
 # Exclude a steady sine from coherence estimation; retain it for autogain.
 grid=np.unique(np.r_[0,.01,.025,.05,.1,np.arange(.25,18.001,.25)])
 energy=[]
 for k in grid:
  values=[]
  for name,x in mats.items():
   if name=='1k sine':continue
   y=render(x,k)
   _,coh=coherence(x,y,48000,nperseg=4096);_,power=welch(y,48000,nperseg=4096)
   values.append(10*np.log10(max(np.sum(power*np.clip(1-coh,0,1))/np.sum(power),1e-30)))
  energy.append(float(np.mean(values)))
 energy=np.maximum.accumulate(energy)
 knobs=np.arange(0,18.001,.25);floor=-40.;floor_knob=1.
 floor_raw=float(np.interp(floor,energy,grid))
 taper=np.array([floor_raw*k/floor_knob if k<floor_knob else np.interp(floor+(energy[-1]-floor)*(k-floor_knob)/(18-floor_knob),energy,grid) for k in knobs]);taper[-1]=18
 meter=pyln.Meter(48000);base={n:meter.integrated_loudness(x) for n,x in mats.items()}
 ag_knobs=np.arange(0,18.001,.5);ag=[];spread=[]
 for k in ag_knobs:
  raw=float(np.interp(k,knobs,taper))
  deltas={n:float(meter.integrated_loudness(render(x,raw))-base[n]) for n,x in mats.items()}
  gain=-float(np.median(list(deltas.values()))) if k else 0.;ag.append(gain)
  spread.append({'knob':float(k),'compensation_db':gain,'residual_lu':{n:v+gain for n,v in deltas.items()}})
 report={'knob_step':.25,'reference_knob':taper.tolist(),'autogain_step':.5,'autogain_db':ag,'method':'Mean nonlinear energy (output-power-weighted 1-coherence, 4096-point Welch windows) across bass 808, bassline, drum bus and dense master. Same metric and -40 dB at knob 1 anchor as Cream. Monotonic envelope inverted for linear nonlinear-energy dB versus knob; continuous linear start. Static autogain targets median integrated LUFS delta of five materials. 48 kHz model rate, Vintage off.','raw_grid':grid.tolist(),'nonlinear_energy_db':energy.tolist(),'materials':spread,'capture_positions':{str(old):float(np.interp(old,taper,knobs)) for old in [3,6,12,18]}}
 (root/'tools/grit_model/calibration.json').write_text(json.dumps(report,indent=2)+'\n')
 print('Capture positions:',report['capture_positions'],flush=True)
 for row in spread[::6]:print(row,flush=True)
