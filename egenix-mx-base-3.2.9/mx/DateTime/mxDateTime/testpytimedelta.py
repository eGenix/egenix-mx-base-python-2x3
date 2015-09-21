from mx.DateTime import now, TimeDelta

if 0:
    # Force import of the datetime module
    d = now()
    d.pydate()

td = TimeDelta(3)
print td
td1 = td.rebuild()
print td1
pytime = td.pytime()
print pytime
pydelta = td.pytimedelta()
print pydelta
