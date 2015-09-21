""" Proxy prototype written in Python. The C version is a little different,
    but this should give a general idea of how the type works.

    Copyright (c) 2000, Marc-Andre Lemburg; mailto:mal@lemburg.com
    Copyright (c) 2000-2015, eGenix.com Software GmbH; mailto:info@egenix.com
    See the documentation for further information on copyrights,
    or contact the author. All Rights Reserved.
"""

import types

class Proxy:

    def __init__(self,object,interface=None):

        # All these private attributes will be hidden in the C
        # implementation
        self.__object = object

        if interface:
            d = {}
            for item in interface:
                if type(item) == types.StringType:
                    d[item] = 1
                else:
                    d[item.__name__] = 1
            self.__interface = d
        else:
            self.__interface = None

        if hasattr(object,'__public_getattr__'):
            self.__public_getattr = object.__public_getattr__
        else:
            self.__public_getattr = None

        if hasattr(object,'__public_setattr__'):
            self.__public_setattr = object.__public_setattr__
        else:
            self.__public_setattr = None

        if hasattr(object,'__cleanup__'):
            self.__cleanup = object.__cleanup__
        else:
            self.__cleanup = None

    def __getattr__(self,what):

        if self.__interface and not self.__interface.has_key(what):
            raise AttributeError,'attribute access restricted'
        if self.__public_getattr:
            return self.__public_getattr(what)
        else:
            return getattr(self.__object,what)

    def __setattr__(self,attr,obj):

        if attr[:8] == '_Proxy__':
            self.__dict__[attr] = obj
            return
        if self.__interface and not self.__interface.has_key(attr):
            raise AttributeError,'attribute access restricted'
        if self.__public_setattr:
            self.__public_setattr(attr,obj)
        else:
            return setattr(self.__object,attr,obj)

    def __del__(self):

        if self.__cleanup:
            self.__cleanup()

class ProxyFactory:

    def __init__(self,Class,interface=()):

        self.Class = Class
        self.interface = interface

    def __call__(self,*args,**kw):

        """ Return a new object 
        """
        return Proxy(apply(self.Class,args,kw),self.interface)

    def __repr__(self):

        return '<ProxyFactory for %s>' % repr(self.Class)

class A:

    a = 1
    b = 2

    def __init__(self):

        self.c = 3

    def __public_getattr__(self,what):

        """ If given, this method is called for all public getattr access
            to the object.
        """
        print 'public getattr',what
        return getattr(self,what)
        
    def __public_setattr__(self,what,to):

        """ If given, this method is called for all public setattr access
            to the object.
        """
        print 'public setattr',what,'to',to
        return setattr(self,what,to)
        
    def write(self,value):
        
        self.a = value

    def read(self):

        return self.a

    def __str__(self):

        return str(self.a)

    def __cleanup__(self):

        """ If given, this method is called if the proxy object gets
            garbage collected. You can use it to break circular references
            the object might have introduced.
        """
        print 'cleanup'
        self.__dict__.clear()

AA = ProxyFactory(A,(A.read,A.write,'__str__'))

o = Proxy(A(),(A.read,A.write,'__str__'))
p = AA()
