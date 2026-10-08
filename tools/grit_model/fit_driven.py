"""Fit the driven output knee to retained captures; no private audio paths required."""
from pathlib import Path
import json
import numpy as np
from scipy.optimize import least_squares
root=Path(__file__).resolve().parent
d=np.load(root/'driven_targets.npz')
f,a,target,usable=(d[k] for k in ['f','a','target','usable'])
# Reference low-level makeup from 1 kHz. Normalize against the minimum setting.
i=np.flatnonzero((f==1000)&np.isclose(a,10**(-30/20)))[0]
gains=20*np.log10(abs(target[:,i,0])/a[i]); gains-=gains[0]
base=json.loads((root/'fit.json').read_text())
phase=2*np.pi*np.arange(8192)/8192
sine=np.sin(phase)
rows=[]
for take in range(1,4):
 for j in range(len(f)):
  # Midrange tones identify output limiting; LF coloration remains in the base model.
  if usable[take,j] and 160<=f[j]<=1000:
   hs=np.array([1,3,5,7,9]); obs=abs(target[take,j,hs-1]); rows.append((take,j,hs,obs))
# Evaluate the base path at each level, including its phase, before fitting the knee.
base_waves={}
for take,j,_,_ in rows:
 z=a[j]*10**(gains[take]/20)*sine
 y=base['direct']*z
 harmonic=np.arange(len(z)//2+1)*f[j]
 for fc,weights in zip(base['poles'],base['weights']):
  shaped=weights[0]*z
  for th,w in zip(base['thresholds'],weights[1:]):shaped+=w*th*np.tanh(z/th)
  y+=np.fft.irfft(np.fft.rfft(shaped)/(1+1j*harmonic/fc),n=len(z))
 for fc,w in zip(base['core_frequencies'],base['core_weights']):
  v=np.fft.irfft(np.fft.rfft(z)/(1+1j*harmonic/fc),n=len(z))*np.sqrt(1+(40/fc)**2)
  v/=np.sqrt(1+v*v);y+=w*v**3
 base_waves[take,j]=y
def predict(p,take,j,hs):
 threshold,knee=p
 z=base_waves[take,j]
 y=z/(1+(abs(z)/threshold)**knee)**(1/knee)
 return abs(np.fft.rfft(y)[hs])*2/len(y)
def residual(p):
 errors=[]
 for t,j,hs,obs in rows:
  pred=predict(p,t,j,hs)
  errors.extend((pred-obs)/max(obs[0],.03))
  errors.append(2*(np.linalg.norm(pred[1:])/pred[0]-np.linalg.norm(obs[1:])/obs[0]))
 return np.array(errors)
fit=least_squares(residual,[.79,20],bounds=([.5,2],[1.2,100]),xtol=1e-12,ftol=1e-12,gtol=1e-12)
p={'makeup_db':gains.tolist(),'threshold':float(fit.x[0]),'knee':float(fit.x[1]),'knob_db':[3,6,12,18], 'fit_note':'Shared output knee from 160/400/1000 Hz magnitude harmonics. Whole capture chain; overload origin not identified.'}
(root/'driven_fit.json').write_text(json.dumps(p,indent=2)+'\n')
print(json.dumps(p,indent=2))
for take in [2,3]:
 for j in np.flatnonzero(f==1000):
  hs=np.array([1,3,5,7,9]);pred=predict(fit.x,take,j,hs);obs=abs(target[take,j,hs-1]);print(take,round(20*np.log10(a[j])), 'gain error dB',round(20*np.log10(pred[0]/obs[0]),3),'THD measured/model %',round(100*np.linalg.norm(obs[1:])/obs[0],3),round(100*np.linalg.norm(pred[1:])/pred[0],3))
