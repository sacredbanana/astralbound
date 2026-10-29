#!/usr/bin/env python3
"""Extract the IAU Catalog of Star Names from a saved WGSN table page.
Usage: python3 tools/import_iau_names.py modern-iau-star-names.html
Source: https://exopla.net/star-names/modern-iau-star-names/ (IAU WGSN).
"""
import csv,html,re,sys,unicodedata
def ascii(s):return unicodedata.normalize('NFKD',s).encode('ascii','ignore').decode()
page=open(sys.argv[1],encoding='utf-8').read()
out=[]
for row in re.findall(r'<tr.*?</tr>',page,re.S):
 c=[html.unescape(re.sub(r'<[^>]+>','',x)).strip() for x in re.findall(r'<t[dh][^>]*>(.*?)</t[dh]>',row,re.S)]
 if len(c)<17 or not c[0] or c[0]=='proper names':continue
 out.append([ascii(c[0]),c[0],'' if c[2]=='-' else c[2],c[3] if c[3].isdigit() else '',c[4],c[13],c[14],c[10].replace('/','-')])
assert len({r[0] for r in out})==len(out)
with open('data/iau-names.csv','w',encoding='utf-8') as f:
 w=csv.writer(f,lineterminator='\n');w.writerow(['name','iau_name','designation','hip','bayer','ra_deg','dec_deg','adopted']);w.writerows(out)
print(len(out),'IAU names')
