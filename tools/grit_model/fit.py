import numpy as np,json
from pathlib import Path
import argparse
parser=argparse.ArgumentParser(description='Fit an experimental parallel Hammerstein model to de-embedded odd harmonic targets.')
parser.add_argument('targets',type=Path)
parser.add_argument('--output',type=Path,default=Path(__file__).with_name('fit.json'))
args=parser.parse_args()
D=np.load(args.targets);f=D['f'];a=D['a'];target=D['target'];hs=np.arange(1,10)
N=1024;theta=np.arange(N)*2*np.pi/N;x=a[:,None]*np.sin(theta)
poles=np.array([8,25,80,250,800,2500,8000.]);thresholds=np.geomspace(.002,2,12)
# Remove a fixed 2.65us capture/path delay from the fitting target; not modeled as plugin latency.
target=target*np.exp(2j*np.pi*f[:,None]*hs*2.65e-6)
base=np.zeros_like(target);base[:,0]=-1j*a
H=1/(1+1j*f[:,None,None]*hs[None,:,None]/poles)
shapes=np.stack([x]+[t*np.tanh(x/t) for t in thresholds],axis=2)
F=np.fft.rfft(shapes,axis=1)[:,1:10,:]*2/N
features=(H[:,:,:,None]*F[:,:,None,:]).reshape(30,9,-1)
# Include direct linear gain only. Output starts at x and fits small corrections.
corefreq=np.array([2.,8.,20.,40.])
corefeatures=[]
for cf in corefreq:
 tf=1/(1+1j*f/cf)*abs(1+1j*40/cf)
 z=a[:,None]*abs(tf)[:,None]*np.sin(theta+np.angle(tf)[:,None])
 shape=z**3/(1+z*z)**1.5
 corefeatures.append(np.fft.rfft(shape,axis=1)[:,1:10]*2/N)
features=np.concatenate([features,base[:,:,None],np.stack(corefeatures,axis=2)],axis=2)
scale=np.maximum(abs(target),a[:,None]*.0002);scale[:,0]=a*.02
scale[:,1:]=np.maximum(scale[:,1:],abs(target[:,2:3])*.4)
mask=(f[:,None]*hs<20000)&(hs[None,:]%2==1)
A=features/scale[:,:,None];b=(target-base)/scale
A=np.r_[A[mask].real,A[mask].imag];b=np.r_[b[mask].real,b[mask].imag]
# Tikhonov penalty on actual shaper deviation sampled over measured and extrapolated amplitudes.
xx=np.geomspace(1e-5,4,200)
sh=np.array([xx]+[t*np.tanh(xx/t) for t in thresholds]).T
reg=np.zeros((len(poles)*len(xx),features.shape[-1]))
for j in range(len(poles)):reg[j*len(xx):(j+1)*len(xx),j*13:(j+1)*13]=sh/np.maximum(xx[:,None],.03)
for lam in [.1]:
 reg[-4:,-4:]+=np.eye(4)*20
 coeff=np.linalg.lstsq(np.r_[A,lam*reg,.01*np.eye(A.shape[1])],np.r_[b,np.zeros(len(reg)+A.shape[1])],rcond=None)[0]
 pred=base+np.einsum('ijk,k->ij',features,coeff)
 err=20*np.log10(abs(pred[:,0]/target[:,0]));thdo=np.linalg.norm(target[:,1:],axis=1)/abs(target[:,0]);thdm=np.linalg.norm(pred[:,1:],axis=1)/abs(pred[:,0])
 print('lambda',lam,'gain err max',max(abs(err)),'THD error median dB',np.median(abs(20*np.log10(thdm/thdo))),'max correction',np.max(abs(reg@coeff)),flush=True)
 if lam==.1:
  args.output.write_text(json.dumps(dict(poles=poles.tolist(),thresholds=thresholds.tolist(),weights=coeff[:91].reshape(len(poles),13).tolist(),direct=1+coeff[91],core_frequencies=corefreq.tolist(),core_weights=coeff[92:].tolist(),delay_removed_seconds=2.65e-6),indent=2))
  for i in range(30):print(f[i],round(20*np.log10(a[i])),round(thdo[i]*100,4),round(thdm[i]*100,4),round(err[i],3))
