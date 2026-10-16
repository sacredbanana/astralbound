#!/usr/bin/env python3
"""Rebuild the compact CC BY-SA HYG extract from a downloaded v4.1 CSV.
Usage: python3 tools/import_stars.py build/catalog/hyg.csv
Coordinates: equatorial J2000, millilightyears, X=RA0, Y=north, Z=RA6h.
"""
import csv, sys, unicodedata
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
import math
ra=math.radians((10+49/60+15.57/3600)*15);dec=math.radians(-53-19/60-6.1/3600)
output.append(['Luhman 16','WISE J104915.57-531906.1',round(6500*math.cos(dec)*math.cos(ra)),round(6500*math.sin(dec)),round(6500*math.cos(dec)*math.sin(ra)),'L8/T1','NASA'])
with open('data/stars.csv','w') as f:
 w=csv.writer(f,lineterminator='\n');w.writerow(['name','aliases','x_mly','y_mly','z_mly','spectra','hyg_id']);w.writerows(output)
print(len(output),'systems')
