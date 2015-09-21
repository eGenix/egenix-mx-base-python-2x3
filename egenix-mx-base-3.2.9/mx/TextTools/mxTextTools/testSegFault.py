#
# Bug (segfault) reported by
# Date: Wed, 25 Jan 2006 14:29:34 +0100
# From: Reinhard Engel <nc-engelre@netcologne.de>
#
from mx.TextTools import *

tagtable = (
    (None, Word, '<p'),
    (None, IsNot, '>', +1, 0),
    (None, Is, '>', MatchFail, MatchOk),
    )
            
# This works
str1 = '<p class="nummer" abc>'
print tag(str1, tagtable)
            
# This segfaults
str2 = '<p class="nummer" abc'
print tag(str2, tagtable)

# Bug #882: Segfault in mxTextTools
# Date: Thu, 17 May 2012 10:12:10 +0100
# From: RICHARD MOSELEY <dickie.moseley@virgin.net>
class Test(object):
    value = ""
    def append(self, value):
        self.value = value
tagobj = Test()
print tag('abbaabccd',((tagobj,AllIn+AppendToTagobj,'ab',0),),0)
assert tagobj.value == (None, 0, 6, None), tagobj.value
tagobj = None
