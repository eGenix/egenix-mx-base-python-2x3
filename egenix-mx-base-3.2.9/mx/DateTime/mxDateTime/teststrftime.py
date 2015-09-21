from mx.DateTime import *

t = now()
for i in range(10000):
    s = t.strftime('%D %T %Z')
d = now() - t

print ('elapsed time: %s output: %r' % (d, s))

# Test long deltas on Windows. See #1349.
d1 = Date(2014,1,1)
d2 = Date(2000,1,1)
delta = d1 - d2
assert delta.days == 5114
print ('Big delta strftime(): %r' % delta.strftime('%d %H:%M'))
