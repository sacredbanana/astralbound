#!/usr/bin/env python3
"""Rebuild the CC BY-SA HYG extract from a downloaded v4.1 CSV.
Usage: python3 tools/import_stars.py build/catalog/hyg.csv
Coordinates: equatorial J2000, millilightyears, X=RA0, Y=north, Z=RA6h.
The original 94 rows come first, unchanged: their order fixes their sector
keys. Every IAU-named star (data/iau-names.csv) and every other HYG proper
name follows, nearest first. Stars HYG cannot place use data/extra-stars.csv.
"""
import csv, math, sys, unicodedata
from pathlib import Path
rows=list(csv.DictReader(open(sys.argv[1])))
skip={'Toliman','Struve 2398 B','Guniibuu B','Alula Australis B','Castor B','Dziban B'}
chosen=sorted((r for r in rows if r['proper'] and r['proper'] not in skip and r['comp']== '1'),key=lambda r:float(r['dist']))[:85]
for r in rows:
 if r['proper'] in {'Polaris','Rigel','Betelgeuse','Antares','Deneb'}:chosen.append(r)
 if r['hip'] in {'8102','104214','108870'}:
  r=dict(r);r['proper']={'8102':'Tau Ceti','104214':'61 Cygni','108870':'Epsilon Indi'}[r['hip']];chosen.append(r)
# HYG does not uniformly encode multiplicity. These components are explicit;
# omitted/unconfirmed companions are not invented from spectral punctuation.
components={'61 Cygni':'K5V/K7V','Epsilon Indi':'K5V/T1/T6','Antares':'M1Ib/B2.5V','Polaris':'F7Ib/F6V/F3V','Rigil Kentaurus':'G2V/K1V','Sirius':'A1V/DA2','Procyon':'F5IV-V/DQZ','Keid':'K0V/DA4/M4V','Groombridge 34':'M1V/M3V','Struve 2398 A':'M3V/M3.5V','Kruger 60':'M3V/M4V','EZ Aqr':'M5V/M5V/M6V','p Eridani':'K2V/K0V','Porrima':'F0V/F0V','Castor':'A1V/M3V/A2V/M2V/M1V/M1V','Capella':'G8III/G0III/M2V/M4V'}
aliases={'Sol':'Sun Solar System','Rigil Kentaurus':'Alpha Centauri A Alpha Centauri B Toliman','Proxima Centauri':'Alpha Centauri C','Ran':'Epsilon Eridani','Keid':'40 Eridani','Alsafi':'Sigma Draconis','Achird':'Eta Cassiopeiae','Copernicus':'55 Cancri','Titawin':'Upsilon Andromedae','Helvetios':'51 Pegasi','Chalawan':'47 Ursae Majoris','Errai':'Gamma Cephei'}
def ascii(s):return unicodedata.normalize('NFKD',s).encode('ascii','ignore').decode()
output=[]
used=set()
for r in chosen:
 name=ascii(r['proper'])
 if name in used: continue
 used.add(name)
 if name=='Rigil Kentaurus': name='Alpha Centauri'
 xyz=[round(float(r[k])*3261.563777) for k in ('x','z','y')]
 if r['proper']=='Sol':xyz=[0,0,0]
 output.append([name,aliases.get(r['proper'],''),*xyz,components.get(r['proper'],r['spect']),r['id']])
# Nearby brown dwarf pair, discovery designation supplies RA/Dec; 6.5 ly from NASA.
ra=math.radians((10+49/60+15.57/3600)*15);dec=math.radians(-53-19/60-6.1/3600)
output.append(['Luhman 16','WISE J104915.57-531906.1',round(6500*math.cos(dec)*math.cos(ra)),round(6500*math.sin(dec)),round(6500*math.cos(dec)*math.sin(ra)),'L8/T1','NASA'])
# Every named star. HYG places what Hipparcos measured; the curated extras
# place the rest. A second name for an included star becomes an alias.
greek={'α':'Alpha','β':'Beta','γ':'Gamma','δ':'Delta','ε':'Epsilon','ϵ':'Epsilon','ζ':'Zeta','η':'Eta','θ':'Theta','ι':'Iota','κ':'Kappa','λ':'Lambda','μ':'Mu','µ':'Mu','ν':'Nu','ξ':'Xi','ο':'Omicron','π':'Pi','ρ':'Rho','σ':'Sigma','τ':'Tau','υ':'Upsilon','φ':'Phi','χ':'Chi','ψ':'Psi','ω':'Omega'}
merged={'Siwarha':'Betelgeuse'} # Betelgeuse's companion, not separately placed
not_stars={'3C 273'} # a quasar
extras={r['name']:r for r in csv.DictReader(open('data/extra-stars.csv',encoding='utf-8'))}
byhip={r['hip']:r for r in rows if r['hip']}
byname={ascii(r['proper']):r for r in rows if r['proper']}
entry={e[0]:e for e in output}
def has(words,name):return (' '+name+' ') in (' '+words+' ')
def alias(e,text):
 if text and not has(e[1],text): e[1]=(e[1]+' '+text).strip()
def place(ra,dec,pc):
 ra,dec,d=math.radians(float(ra)),math.radians(float(dec)),float(pc)*3261.563777
 return [round(d*math.cos(dec)*math.cos(ra)),round(d*math.sin(dec)),round(d*math.cos(dec)*math.sin(ra))]
def known(name):return name in entry or any(has(e[1],name) for e in output)
added=[]
byid={e[6]:e for e in output}
iau=list(csv.DictReader(open('data/iau-names.csv',encoding='utf-8')))
# Two IAU names on one Hipparcos star: HYG's proper name leads.
iau.sort(key=lambda r:r['hip'] not in byhip or ascii(byhip[r['hip']]['proper'])!=r['name'])
for r in iau:
 name=r['name']
 bayer=''.join(greek.get(ch,ch) for ch in r['bayer']).strip()
 also=' '.join(dict.fromkeys(x for x in (bayer,extras[name]['designation'] if name in extras else r['designation']) if x and x!=name))
 if name in entry: alias(entry[name],also)
 if known(name) or name in merged: continue
 h=byhip.get(r['hip']) or byname.get(name)
 if h and h['id'] in byid: alias(byid[h['id']],name);continue
 if name in extras:
  x=extras[name];e=[name,also,*place(x['ra_deg'],x['dec_deg'],x['dist_pc']),x['spectra'],'IAU']
 elif h and float(h['dist'])<100000:
  e=[name,also,*[round(float(h[k])*3261.563777) for k in ('x','z','y')],h['spect'].split('/')[0].strip() or 'G','%s'%h['id']]
 else: sys.exit('no position for '+name)
 added.append(e);byid[e[6]]=e;entry[name]=e
for r in rows:
 name=ascii(r['proper'])
 if not name or r['comp']!='1' or name in not_stars or known(name) or r['id'] in byid or float(r['dist'])>=100000: continue
 e=[name,'',*[round(float(r[k])*3261.563777) for k in ('x','z','y')],r['spect'].split('/')[0].strip() or 'G',r['id']]
 added.append(e);byid[r['id']]=e;entry[name]=e
for name,host in merged.items(): alias(entry[host],name)
added.sort(key=lambda e:(e[2]*e[2]+e[3]*e[3]+e[4]*e[4],e[0]))
output+=added
with open('data/stars.csv','w') as f:
 w=csv.writer(f,lineterminator='\n');w.writerow(['name','aliases','x_mly','y_mly','z_mly','spectra','hyg_id']);w.writerows(output)
print(len(output),'systems')
