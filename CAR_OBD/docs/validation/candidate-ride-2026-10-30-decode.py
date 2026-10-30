from pathlib import Path
from collections import Counter,defaultdict
import re,json,hashlib,statistics,csv,bisect
base=Path('docs/validation/candidate-ride-2026-10-30')
raw=base.with_suffix('.txt');report=base.with_suffix('.diagnostics.txt')
lines=raw.read_text().splitlines();rlines=report.read_text().splitlines()
assert lines[0].startswith('LOG_BEGIN ') and lines[-1]=='LOG_END'
assert rlines[0].startswith('DIAG_REPORT_BEGIN ') and rlines[-1]=='DIAG_REPORT_END'
fields=lambda text:dict(re.findall(r'(\w+)=([^ ]+)',text))
rows=[]
for line in lines:
 m=re.fullmatch(r'(\d+) boot=(\d+) ms=(\d+) (.*)',line)
 if m:rows.append(dict(seq=int(m[1]),boot=int(m[2]),ms=int(m[3]),text=m[4]))
assert all(a['seq']+1==b['seq'] for a,b in zip(rows,rows[1:]))
boots=[]
for boot,count in Counter(x['boot'] for x in rows).items():
 subset=[x for x in rows if x['boot']==boot]
 boots.append(dict(boot=boot,records=count,first_ms=subset[0]['ms'],last_ms=subset[-1]['ms'],first_sequence=subset[0]['seq'],last_sequence=subset[-1]['seq']))
print('RAW',len(rows),'seq',rows[0]['seq'],rows[-1]['seq'],'BOOTS',boots)
session=int(fields(rlines[0])['session'])
car=Counter(x['boot'] for x in rows if x['text'].startswith('TCM_CANDIDATE ')).most_common(1)[0][0]
carrows=[x for x in rows if x['boot']==car]
items=[fields(x) for x in rlines if x.startswith('DIAG_ITEM ')]
samples=[];latest={};contexts={};results=[];current_result=None
for x in carrows:
 text=x['text']
 if text.startswith('DIAG_RESULT '):
  current_result=dict(x,**fields(text));results.append(current_result)
 if text.startswith('DIAG_DATA '):
  f=fields(text);item=int(f['item']);offset=int(f['offset']);part=bytes.fromhex(f['bytes'])
  if offset==0:
   sample=dict(seq=x['seq'],boot=car,ms=x['ms'],item=item,request=f['request'],payload=part.hex().upper(),context={})
   samples.append(sample);latest[item]=sample
  else:
   sample=latest[item];assert len(sample['payload'])//2==offset
   sample['payload']+=part.hex().upper()
 if text.startswith('CANDIDATE_CONTEXT '):
  f=fields(text);item=int(f.pop('item'));sample=latest[item]
  assert 0<=x['ms']-sample['ms']<=100
  sample['context'].update({k:int(v) for k,v in f.items()})
tcm=[];alt=[];engine=[]
for sample in samples:
 p=bytes.fromhex(sample['payload']);item=sample['item']
 if item==269:
  assert len(p)==28
  sample.update(gear=p[21],commanded_raw=p[22],commanded_low_nibble=p[22]&15,selector_byte20=p[20],atf_candidate_C=p[13]-40,tcm_rpm=int.from_bytes(p[4:6],'big')/4,tcm_speed=p[6])
  tcm.append(sample)
 elif item==270:
  assert len(p)==50
  sample.update(published_gear_byte4=p[4]);alt.append(sample)
 elif item==280:
  assert len(p)==38
  sample.update(oil_candidate_C=p[34]*.75-48);engine.append(sample)
print('REPORT',rlines[:3])
print('RETAINED samples',len(tcm),len(alt),len(engine),'results',Counter((x['request'],x['status']) for x in results))
print('GEARS',dict(Counter(s['gear'] for s in tcm)))
print('SELECTOR',dict(Counter(s['selector_byte20'] for s in tcm)))
print('SELECTOR/GEAR',dict(Counter((s['selector_byte20'],s['gear']) for s in tcm)))
print('COMMAND',dict(Counter(s['commanded_raw'] for s in tcm)))
print('ALT BYTE4',dict(Counter(s['published_gear_byte4'] for s in alt)))
print('TEMPS ATF minmax',min(s['atf_candidate_C'] for s in tcm),max(s['atf_candidate_C'] for s in tcm),'oil',min(s['oil_candidate_C'] for s in engine),max(s['oil_candidate_C'] for s in engine))
print('FINAL TCM')
for s in tcm:
 if s['ms']>tcm[-1]['ms']-70000:
  print(f"boot+{s['ms']/1000:.3f} elapsed={s['context'].get('elapsed_ms',0)/1000:.3f} speed={s['context'].get('speed_kmh')} sel20={s['selector_byte20']:02X} gear={s['gear']:02X} cmd={s['commanded_raw']:02X} b23..27={s['payload'][46:]} RPM={s['tcm_rpm']}")
print('FINAL ALT')
for s in alt:
 if s['ms']>tcm[-1]['ms']-70000:print(s['ms']/1000,s['context'].get('elapsed_ms',0)/1000,s['context'].get('speed_kmh'),s['payload'])
print('NOTABLE')
notable=[x for x in carrows if any(p in x['text'] for p in ('DIAG_MODE','BT_RETRY','BT_CLOSED','ELM_ERROR','MONITOR_ERROR','LOG_QUEUE_OVERFLOW','status=FORMAT_ERROR','status=TIMEOUT','WARNING ','UI_MODE'))]
for x in notable:print(x['ms']/1000,x['text'])
speedrows=[x for x in carrows if x['text'].startswith('SPEED valid=1')]
gaps=[b['ms']-a['ms'] for a,b in zip(speedrows,speedrows[1:])]
print('SPEED',len(speedrows),'duration',(speedrows[-1]['ms']-speedrows[0]['ms'])/1000,'gap median',statistics.median(gaps),'max',max(gaps))
print('HEALTH tail',[x['text'] for x in carrows if 'HEALTH' in x['text']][-4:])
print('SAMPLE firstlast',tcm[0]['ms'],tcm[-1]['ms'])
# Independent cross-check of decoded fields against the firmware's numeric log lines.
logged=[fields(x['text']) for x in carrows if x['text'].startswith('TCM_CANDIDATE ')]
assert len(logged)==len(tcm)
for s,f in zip(tcm,logged):
 assert s['gear']==int(f['gear_raw']) and s['commanded_raw']==int(f['commanded_raw'])
 assert s['atf_candidate_C']==int(f['ATF_candidate_C']) and s['tcm_speed']==int(f['speed_candidate_kmh'])
# Structured artifacts preserve every recovered sample and contextual reference age.
summary=dict(raw_sha256=hashlib.sha256(raw.read_bytes()).hexdigest(),report_sha256=hashlib.sha256(report.read_bytes()).hexdigest(),records=len(rows),first_sequence=rows[0]['seq'],last_sequence=rows[-1]['seq'],boots=boots,car_boot=car,report_header=fields(rlines[0]),trip=fields(rlines[1]),gear_values=fields(rlines[2]),items=items,results=[dict(seq=x['seq'],ms=x['ms'],request=x['request'],status=x['status']) for x in results],tcm=tcm,alternative_tcm=alt,engine=engine,speed_samples=len(speedrows),speed_gap_median_ms=statistics.median(gaps),speed_gap_max_ms=max(gaps),notable=notable)
base.with_name(base.name+'-summary').with_suffix('.json').write_text(json.dumps(summary,indent=2)+'\n')
with base.with_name(base.name+'-tcm').with_suffix('.csv').open('w',newline='') as f:
 columns=['seq','boot','ms','elapsed_ms','speed_kmh','speed_age_ms','gear','commanded_raw','commanded_low_nibble','selector_byte20','atf_candidate_C','tcm_rpm','tcm_speed','rpm','rpm_age_ms','coolant_C','coolant_age_ms','intake_C','intake_age_ms','payload']
 writer=csv.DictWriter(f,fieldnames=columns);writer.writeheader()
 for s in tcm:writer.writerow({k:s.get(k,s['context'].get(k,'')) for k in columns})
# State timing; selector labels are correlated with the driver-confirmed D -> N -> R -> P sequence.
runs=[]
for x in tcm:
 if not runs or runs[-1]['selector']!=x['selector_byte20']:
  runs.append(dict(selector=x['selector_byte20'],start_ms=x['ms'],end_ms=x['ms'],elapsed_start_ms=x['context']['elapsed_ms'],elapsed_end_ms=x['context']['elapsed_ms'],samples=1,gears=[x['gear']]))
 else:
  run=runs[-1];run['end_ms']=x['ms'];run['elapsed_end_ms']=x['context']['elapsed_ms'];run['samples']+=1
  run['gears']=sorted(set(run['gears']+[x['gear']]))
summary['selector_runs']=runs
rpm_differences=[abs(x['tcm_rpm']-x['context']['rpm']) for x in tcm if x['context']['rpm_valid'] and x['context']['rpm_age_ms']<1500]
summary['comparisons']=dict(command_low_nibble_matches=sum(x['gear']==x['commanded_low_nibble'] for x in tcm if 1<=x['gear']<=6),forward_gear_samples=sum(1<=x['gear']<=6 for x in tcm),speed_difference_median=statistics.median(abs(x['tcm_speed']-x['context']['speed_kmh']) for x in tcm),speed_difference_max=max(abs(x['tcm_speed']-x['context']['speed_kmh']) for x in tcm),fresh_rpm_samples=len(rpm_differences),fresh_rpm_absolute_difference_median=statistics.median(rpm_differences))
summary['primary_sample_gap_median_ms']=statistics.median(b['ms']-a['ms'] for a,b in zip(tcm,tcm[1:]))
summary['primary_sample_gap_max_ms']=max(b['ms']-a['ms'] for a,b in zip(tcm,tcm[1:]))
summary['long_primary_gaps']=[dict(last_good_ms=a['ms'],next_good_ms=b['ms'],gap_ms=b['ms']-a['ms']) for a,b in zip(tcm,tcm[1:]) if b['ms']-a['ms']>10000]
summary['temperature_comparisons']={}
for byte,field in [(25,'coolant_C'),(33,'intake_C')]:
 diff=[abs(bytes.fromhex(x['payload'])[byte]*.75-48-x['context'][field]) for x in engine if x['context'][field.replace('_C','_valid')]]
 summary['temperature_comparisons'][field]=dict(samples=len(diff),median_absolute_difference_C=statistics.median(diff),max_absolute_difference_C=max(diff))
summary['candidate_temperature_endpoints']={}
for itemid,name in [(269,'ATF_C'),(280,'oil_C')]:
 item=next(x for x in items if int(x['id'])==itemid)
 values={}
 for key in ('first','last'):
  p=bytes.fromhex(item[key]);values[key]=p[13]-40 if itemid==269 else p[34]*.75-48
 summary['candidate_temperature_endpoints'][name]=values
summary['selector_hypothesis']={'byte_offset': 20, 'request': '21A0', 'tx': '7E1', 'values': {'8': 'D', '9': 'N', '10': 'R', '11': 'P'}, 'user_confirmed_final_sequence': True, 'user_sequence': ['D', 'N', 'R', 'P'], 'validation': 'driver-correlated on the tested vehicle', 'factory_mapping_verified': False, 'note': 'User stopped in D around 9:42, then selected N, R, P. Recorded states 8 -> 9 -> 10 -> 11 match; the run correctly ends in P.'}
base.with_name(base.name+'-summary').with_suffix('.json').write_text(json.dumps(summary,indent=2)+'\n')

summary['commanded_shift_examples']=[dict(ms=x['ms'],current=x['gear'],commanded=x['commanded_low_nibble'],next_current=tcm[i+1]['gear'],next_sample_ms=tcm[i+1]['ms']) for i,x in enumerate(tcm[:-1]) if 1<=x['gear']<=6 and x['gear']!=x['commanded_low_nibble']]
assert all(x['commanded']==x['next_current'] for x in summary['commanded_shift_examples'])
base.with_name(base.name+'-summary').with_suffix('.json').write_text(json.dumps(summary,indent=2)+'\n')
