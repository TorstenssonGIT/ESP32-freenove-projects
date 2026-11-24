#!/usr/bin/env python3
# Renders lab8_chart_bandwidth_r.png from lab8_bandwidth_log.csv.
# Each (date,run) is one r-run; nodes B/C. Re-run after appending to the CSV.
import csv, collections, matplotlib
matplotlib.use('Agg'); import matplotlib.pyplot as plt

rows=list(csv.DictReader(open('/home/claude/lab8_bandwidth_log.csv')))
sizes=sorted({int(r['size']) for r in rows})
xi={s:i for i,s in enumerate(sizes)}
def stamp(r):  # "date time" when a real NTP time was logged, else just the date
    t=r.get('time','') or ''
    return f"{r['date']} {t}" if t and t!='time-unknown' else r['date']
# group: key=(run,node) -> {size:(kbps,rtt,lost)}; keep each node's own stamp
G=collections.OrderedDict(); STAMP={}
for r in rows:
    key=(int(r['run']),r['node'])
    G.setdefault(key,{})[int(r['size'])]=(int(r['kbps']),int(r['rtt_med_us']),int(r['lost']))
    STAMP[key]=stamp(r)
runs=sorted({rn for (rn,n) in G})          # one entry per r-run (B and C share it)
COL={'B':'#d97706','C':'#059669'}
INK='#1f2430'; MUT='#6b7280'; GRID='#e5e7eb'
# older runs lighter, newest solid
alphas={rn:(0.4+0.6*(i+1)/len(runs)) for i,rn in enumerate(runs)}

fig,(ax1,ax2)=plt.subplots(2,1,figsize=(7.6,6.2),dpi=170,sharex=True,
                           gridspec_kw={'height_ratios':[1,1],'hspace':0.14})
fig.patch.set_facecolor('white')
for ax in (ax1,ax2):
    ax.set_facecolor('white')
    for s in ('top','right'): ax.spines[s].set_visible(False)
    for s in ('left','bottom'): ax.spines[s].set_color(GRID)
    ax.tick_params(colors=MUT,labelsize=9,length=0)
    ax.grid(axis='y',color=GRID,linewidth=1); ax.set_axisbelow(True)

multi=len(runs)>1
for (rn,n),data in G.items():
    xs=[xi[s] for s in sizes]
    mb=[data[s][0]/1000 for s in sizes]; ms=[data[s][1]/1000 for s in sizes]
    a=alphas[rn] if multi else 1.0
    lab=(f"{n} r{rn} – {STAMP[(rn,n)]}" if multi else f"{n} – {STAMP[(rn,n)]}")
    ax1.plot(xs,mb,'-o',color=COL[n],lw=2,ms=5,alpha=a,label=lab)
    ax2.plot(xs,ms,'-o',color=COL[n],lw=2,ms=5,alpha=a)

nruns=len(runs)
ax1.set_ylabel('Throughput (Mbit/s)',color=INK,fontsize=10); ax1.set_ylim(0,3.4)
ttl=f'Lab 8 – 3-leg bandwidth test (r), 100 UDP packets/size, 0 lost'
ax1.set_title(ttl,color=INK,fontsize=11,fontweight='bold',loc='left',pad=10)
ax1.legend(frameon=False,fontsize=8.5,labelcolor=INK,loc='upper left',ncol=1 if nruns<=1 else 2)
ax2.set_ylabel('Median RTT (ms)',color=INK,fontsize=10); ax2.set_ylim(0,8)
ax2.set_xlabel('UDP payload size (bytes)',color=INK,fontsize=10)
ax2.set_xticks(range(len(sizes))); ax2.set_xticklabels(sizes)

fig.savefig('/home/claude/photos/lab8_chart_bandwidth_r.png',bbox_inches='tight',facecolor='white')
import shutil; shutil.copy('/home/claude/photos/lab8_chart_bandwidth_r.png','/home/claude/md/lab8_chart_bandwidth_r.png')
print(f'rendered {nruns} run(s):', ', '.join(f'r{rn} {STAMP[(rn,"B")]}' for rn in runs if (rn,"B") in STAMP))
