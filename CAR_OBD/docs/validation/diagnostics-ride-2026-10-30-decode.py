import re,json,hashlib,statistics
from pathlib import Path
from collections import Counter,defaultdict
base=Path('docs/validation/diagnostics-ride-2026-10-30')
raw=base.with_suffix('.txt');report=base.with_suffix('.diagnostics.txt')
rows=[]
for line in raw.read_text().splitlines():
 m=re.fullmatch(r'(\d+) boot=(\d+) ms=(\d+) (.*)',line)
 if m: rows.append(dict(seq=int(m[1]),boot=int(m[2]),ms=int(m[3]),text=m[4]))
assert raw.read_text().splitlines()[0].startswith('LOG_BEGIN ') and raw.read_text().splitlines()[-1]=='LOG_END'
assert report.read_text().splitlines()[0].startswith('DIAG_REPORT_BEGIN ') and report.read_text().splitlines()[-1]=='DIAG_REPORT_END'
assert all(a['seq']+1==b['seq'] for a,b in zip(rows,rows[1:]))
items=[dict(re.findall(r'(\w+)=([^ ]+)',s)) for s in report.read_text().splitlines() if s.startswith('DIAG_ITEM ')]
car=next(x['boot'] for x in rows if 'DIAG_RESULT session=1' in x['text'])
carrows=[x for x in rows if x['boot']==car]
transactions=[];current=None;header='7DF';lastspeed=None
unescape=lambda s:re.sub(r'\\(r|n|\\|x[0-9A-Fa-f]{2})',lambda m:{'r':'\r','n':'\n','\\':'\\'}.get(m[1],chr(int(m[1][1:],16)) if m[1].startswith('x') else ''),s)
for x in carrows:
 s=x['text']
 if s.startswith('SPEED valid=1 kmh='): lastspeed=int(re.search(r'kmh=(\d+)',s)[1])
 if s.startswith('TX '):
  cmd=unescape(s[3:]).strip()
  if cmd.startswith('ATSH'):header=cmd[4:]
  current=dict(seq=x['seq'],ms=x['ms'],command=cmd,header=header,nearest_prior_speed_kmh=lastspeed,response='')
 elif s.startswith('RX ') and current:
  current['response']+=unescape(s[3:])
  if '>' in current['response']:
   current['response_ms']=x['ms']-current['ms'];transactions.append(current);current=None

def decode(tx):
 lines=[s.replace(' ','') for s in tx['response'].replace('>','').splitlines() if s.strip()]
 if not lines or not re.fullmatch('[0-9A-Fa-f]{3}',lines[0]):return None
 declared=int(lines[0],16);data=b'';padding=b''
 for i,line in enumerate(lines[1:]):
  m=re.fullmatch(r'([0-9A-Fa-f]):([0-9A-Fa-f]+)',line)
  if not m or int(m[1],16)!=i%16:return None
  part=bytes.fromhex(m[2]);expected=min(6 if i==0 else 7,declared-len(data))
  if expected<=0 or len(part)<expected or len(part)>(6 if i==0 else 7):return None
  data+=part[:expected];padding+=part[expected:]
 if len(data)!=declared:return None
 command=bytes.fromhex(tx['command']);prefix=bytes([command[0]+0x40])+command[1:]
 if not data.startswith(prefix):return None
 return dict(payload=data[len(prefix):].hex().upper(),declared=declared,padding=padding.hex().upper())
recovered=[]
for tx in transactions:
 if tx['command'] in ('21A0','21A1','2101','2103','0902'):
  parsed=decode(tx)
  if parsed:
   tx.update(parsed);recovered.append(tx)
print('Snapshot:',len(rows),'records',rows[0]['seq'],rows[-1]['seq'],'boots',dict(Counter(x['boot'] for x in rows)))
print('Car boot',car,'events',len(carrows))
print('Recovered positive multiline:',dict(Counter((x['header'],x['command']) for x in recovered)))
for tx in recovered:
 if tx['command']=='0902':continue
 p=bytes.fromhex(tx['payload']);data={}
 if tx['header']=='7E1' and tx['command']=='21A0':
  data={k:(p[i] if i<len(p) else None) for k,i in [('speed_raw',6),('ATF_raw',13),('gear_candidate_raw',21),('commanded_candidate_raw',22)]}
  data['rpm_candidate']=int.from_bytes(p[4:6],'big')/4
 if tx['header']=='7E1' and tx['command']=='21A1':data={'gear_candidate_raw':p[4]}
 if tx['command']=='2101':data={'oil_temp_candidate_C':p[34]*.75-48 if len(p)>34 else None}
 if tx['command']=='2103':data={'odometer_offset57_available':len(p)>60}
 tx['candidate_fields']=data
 print(f"{tx['ms']/1000:.3f}s {tx['header']} {tx['command']} speed~{tx['nearest_prior_speed_kmh']} {data} pad={tx['padding']} payload={tx['payload']}")
speeds=[x for x in carrows if x['text'].startswith('SPEED valid=1')]
gaps=[b['ms']-a['ms'] for a,b in zip(speeds,speeds[1:])]
print('Speeds',len(speeds),'min/max',min(int(re.search(r'kmh=(\d+)',x['text'])[1]) for x in speeds),max(int(re.search(r'kmh=(\d+)',x['text'])[1]) for x in speeds),'gap median/max',statistics.median(gaps),max(gaps))
print('Errors:',[(x['ms'],x['text']) for x in carrows if any(s in x['text'] for s in ('ELM_ERROR','MONITOR_ERROR','LOG_QUEUE_OVERFLOW','BT_RETRY','BT_CLOSED','DIAG_MODE','DIAG_REPORT_LOAD'))])
print('Health tail:',[x['text'] for x in carrows if 'HEALTH' in x['text']][-6:])
summary=dict(raw_sha256=hashlib.sha256(raw.read_bytes()).hexdigest(),report_sha256=hashlib.sha256(report.read_bytes()).hexdigest(),records=len(rows),first_sequence=rows[0]['seq'],last_sequence=rows[-1]['seq'],car_boot=car,standard_outcomes=dict(Counter(x['status'] for x in items if int(x['id'])<256)),custom_outcomes=dict(Counter(x['status'] for x in items if int(x['id'])>=269)),speed_samples=len(speeds),speed_gap_median_ms=statistics.median(gaps),speed_gap_max_ms=max(gaps),items=items,recovered_multiline=recovered)
base.with_name(base.name+'-summary').with_suffix('.json').write_text(json.dumps(summary,indent=2)+'\n')
