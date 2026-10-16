#!/usr/bin/env python3
"""Extract NASA/JPL satellite elements/physical data from downloaded HTML.
python3 tools/import_solar.py build/catalog/satellites.html build/catalog/satellite-physical.html
No dependency beyond Python. Preserve source precision and unknown values.
"""
import csv,sys
from html.parser import HTMLParser
class Tables(HTMLParser):
 def __init__(self):super().__init__();self.cell=False;self.rows=[];self.row=[];self.txt=''
 def flush(self):
  if self.cell:self.row.append(' '.join(self.txt.split()));self.cell=False
 def handle_starttag(self,t,a):
  if t=='tr':self.flush();self.row=[]
  if t in ('td','th'):self.flush();self.cell=True;self.txt=''
 def handle_data(self,d):
  if self.cell:self.txt+=d
 def handle_endtag(self,t):
  if t in ('td','th'):self.flush()
  if t=='tr':
   self.flush()
   if self.row:self.rows.append(self.row)
def read(p):
 parser=Tables();parser.feed(open(p).read());return parser.rows
physical={r[2]:r for r in read(sys.argv[2]) if len(r)>=12 and r[2].isdigit()}
rows=[]
for r in read(sys.argv[1]):
 if len(r)!=20 or not r[0].isdigit():continue
 p=physical.get(r[3]);radius=p[6] if p else '';gm=p[3] if p else ''
 rows.append([r[2],r[1],r[3],r[7],r[13],r[11],r[10],radius,gm,r[5],r[6],r[8],r[9],r[12],r[16],r[17]])
# Puck is listed under two alternative ephemerides in the source table.
# Keep the final published solution for each NAIF identity.
rows=list({row[2]:row for row in rows}.values())
with open('data/solar-moons.csv','w') as f:
 w=csv.writer(f,lineterminator='\n');w.writerow(['name','parent','naif','a_km','period_days','inclination_deg','mean_anomaly_deg','radius_km','gm_km3_s2','frame','epoch_tdb','eccentricity','periapsis_deg','node_deg','pole_ra_deg','pole_dec_deg']);w.writerows(rows)
print(len(rows),'moons;',len(physical),'physical records')
