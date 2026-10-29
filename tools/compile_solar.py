#!/usr/bin/env python3
"""Compile the checked-in JPL Solar System extract; no network needed."""
import csv,json,math,datetime
from pathlib import Path
AU=149597870700
prim=list(csv.DictReader(open('data/solar-primary.csv')))
moon=list(csv.DictReader(open('data/solar-moons.csv')))
legacy=list(csv.DictReader(open('data/solar-v8-layout.csv')))
legacy_ids={r['name']:int(r['old_index']) for r in legacy}
active_names=[r['name'] for r in prim]+[r['name'] for r in moon]
def angle(d):return round(float(d)*1024/360)
def matmul(a,b):return [[sum(a[i][k]*b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
def rz(deg):
 c=math.cos(math.radians(deg));sn=math.sin(math.radians(deg));return [[c,-sn,0],[sn,c,0],[0,0,1]]
def rx(deg):
 c=math.cos(math.radians(deg));sn=math.sin(math.radians(deg));return [[1,0,0],[0,c,-sn],[0,sn,c]]
OBLIQUITY=23.4392911
def plane(inc,node,peri,ref=None):
 """Orbit plane in the ecliptic frame: columns periapsis, its quarter turn
 ahead, and the orbit pole."""
 m=matmul(matmul(rz(float(node)),rx(float(inc))),rz(float(peri)))
 if ref:
  ra,dec=map(math.radians,ref)
  # Pole = (sin i sin node, -sin i cos node, cos i).
  pole=(math.cos(dec)*math.cos(ra),math.cos(dec)*math.sin(ra),math.sin(dec))
  rn=math.degrees(math.atan2(pole[0],-pole[1]));ri=math.degrees(math.acos(pole[2]))
  m=matmul(rx(-OBLIQUITY),matmul(matmul(rz(rn),rx(ri)),m))
 return m
def column(m,j):return [m[i][j] for i in range(3)]
# Game x/y/z = ecliptic X/Z/Y, as the star catalogue maps equatorial X/Z/Y:
# the swap matches the renderer's left-handed camera, so orbits run
# anticlockwise seen from ecliptic north (+y), as they do.
def game(v):return [v[0],v[2],v[1]]
def cross3(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def norm(v):
 l=math.sqrt(sum(x*x for x in v));return [x/l for x in v]
def equatorial(ra,dec):
 """An ICRF direction in the ecliptic frame."""
 ra,dec=math.radians(ra),math.radians(dec);e=math.radians(OBLIQUITY)
 x,y,z=math.cos(dec)*math.cos(ra),math.cos(dec)*math.sin(ra),math.sin(dec)
 return [x,math.cos(e)*y+math.sin(e)*z,-math.sin(e)*y+math.cos(e)*z]
def basis(m):
 # First basis multiplies local sine, second cosine.
 return [round(v*16384) for j in (1,0) for v in game(column(m,j))]
def spin_frame(pole,node):
 """Body axes in game coordinates (Q30, row-major) for a positive pole and
 the prime meridian's direction at W = 0 (ecliptic frame). North is body
 -z: the swap to game coordinates is a reflection, and this keeps a body's
 own frame proper."""
 pole=norm(pole);node=norm(node)
 cols=[game(node),game(cross3(pole,node)),[-x for x in game(pole)]]
 return [[cols[j][i] for j in range(3)] for i in range(3)]
def facing(frame,d):
 """Spin angle that turns the prime meridian onto direction d."""
 x=sum(frame[i][0]*d[i] for i in range(3));y=sum(frame[i][1]*d[i] for i in range(3))
 return math.degrees(math.atan2(y,x))%360
def mean_direction(m,phase):
 # catalog_orbit at zero eccentricity: sin M along the second column, cos M the first.
 a=math.radians(angle(phase)*360/1024)
 return game([math.sin(a)*m[i][1]+math.cos(a)*m[i][0] for i in range(3)])
poles={r['name']:r for r in csv.DictReader(open('data/solar-poles.csv'))}
def rotation(name,m,phase,toward=None):
 """Tilt frame and the prime meridian's angle at J2000 (tick 0). Planets
 follow the IAU rotation models, retrograde ones about their positive
 (right-hand) pole. Synchronous moons spin about their orbit pole with the
 prime meridian on the mean direction of their primary; the Moon keeps its
 IAU pole and so its librations; Pluto turns its meridian towards Charon."""
 r=poles.get(name)
 if r:
  ra=float(r['pole_ra_deg'])
  pole=equatorial(ra,float(r['pole_dec_deg']))
  # IAU W counts from the equator's ascending node on the ICRF equator, at
  # right ascension ra + 90 (defined even for Earth's pole at dec 90).
  node=equatorial(ra+90,0)
  rate=float(r['rate_deg_per_day'] or 0)
  if rate<0: pole=[-x for x in pole];node=[-x for x in node]
 else:
  pole=column(m,2);node=column(m,0)
 frame=spin_frame(pole,node)
 if toward is not None: w=facing(frame,toward)
 elif r and r['w0_deg']=='': w=facing(frame,[-x for x in mean_direction(m,phase)])
 elif r: w=float(r['w0_deg']) if rate>=0 else (180-float(r['w0_deg']))%360
 else: w=facing(frame,[-x for x in mean_direction(m,phase)])
 return [round(frame[i][j]*(1<<30)) for i in range(3) for j in range(3)],round(w/360*(1<<32))%(1<<32)
def entry(name,parent,a,period,radius,gravity,typ,clim,atm,inc,phase,e,node,peri,spin,known,ref=None,gases="AIR_NONE",haze="HAZE_NONE",toward=None):
 m=plane(inc,node,peri,ref)
 tilt,w=rotation(name,m,phase,toward)
 return '    {%s,%d,%dLL,%dULL,%d,%d,%s,%s,%d,%d,%d,%d,%d,%d,%d,%d,{%s},%d,%s,%s,{%s},%dU},'%(json.dumps(name),parent,round(a),round(period),round(radius),gravity,typ,clim,round(atm),angle(inc),angle(phase),round(float(e)*1000000),angle(node),angle(peri),round(spin),known,','.join(map(str,basis(m))),legacy_ids[name],gases,haze,','.join(map(str,tilt)),w)
def epoch_shift(r):
 # Catalog epochs are TDB calendar dates. Advance each mean anomaly to J2000.
 d=r['epoch_tdb'].split('.')
 epoch=datetime.datetime.fromisoformat(d[0])+datetime.timedelta(days=float('0.'+d[1]) if len(d)>1 else 0)
 dt=(datetime.datetime(2000,1,1,12)-epoch).total_seconds()/86400
 return (float(r['mean_anomaly_deg'])+dt*360/float(r['period_days']))%360
lines=['/* NASA/JPL catalog; see data/README.md. Generated by tools/compile_solar.py. */','static const SolarBody solar_bodies[] = {']
# Pluto and Charon are locked to each other: Pluto turns once per Charon orbit.
charon=next(r for r in moon if r['name']=='Charon')
charon_plane=plane(charon['inclination_deg'],charon['node_deg'],charon['periapsis_deg'],(132.993,-6.163))
for r in prim:
 if r['name']=='Pluto':
  r=dict(r,spin_hours=str(float(charon['period_days'])*24))
 lines.append(entry(r['name'],-1,float(r['a_au'])*AU,float(r['period_days'])*86400,float(r['radius_km'])*1000,int(r['gravity']),r['type'],r['climate'],float(r['atmosphere_km'])*1000,r['inclination_deg'],float(r['mean_longitude_deg'])-float(r['perihelion_deg']),r['eccentricity'],r['node_deg'],float(r['perihelion_deg'])-float(r['node_deg']),float(r['spin_hours'])*3600,1,gases=r['gases'],haze=r['haze'],toward=mean_direction(charon_plane,epoch_shift(charon)) if r['name']=='Pluto' else None))
for r in moon:
 parent=next(i for i,p in enumerate(prim) if p['name']==r['parent'])
 rad=float(r['radius_km'])*1000 if r['radius_km'] else 1000
 gravity=round(float(r['gm_km3_s2'].lstrip('<>'))*1e9/rad**2) if r['gm_km3_s2'] else 1
 lines.append(entry(r['name'],parent,float(r['a_km'])*1000,float(r['period_days'])*86400,rad,max(1,gravity),'MOON',r['climate'],float(r['atmosphere_km'])*1000,r['inclination_deg'],epoch_shift(r),r['eccentricity'],r['node_deg'],r['periapsis_deg'],float(r['period_days'])*86400,1 if r['radius_km'] else 2,(float(r['pole_ra_deg']),float(r['pole_dec_deg'])) if r['frame']=='Laplace' else ({'Uranus':(257.311,-15.175),'Pluto':(132.993,-6.163)}[r['parent']]) if r['frame']=='equatorial' else None,gases=r['gases'],haze=r['haze']))
lines+=['};','static const int16_t solar_v8_to_current[] = {',
    '    '+','.join(str(active_names.index(r['name'])) if r['name'] in active_names else '-1' for r in legacy), '};']
assert len(active_names)==34 and len(set(active_names))==34
assert set(r["name"] for r in moon)==set(json.loads(Path("data/solar-selection.json").read_text())["moons"])
Path('src/solar_catalog.inc').write_text('\n'.join(lines)+'\n')
print(len(active_names),'bodies,',len(moon),'major moons')
