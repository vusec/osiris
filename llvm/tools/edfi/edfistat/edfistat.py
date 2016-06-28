#!/usr/bin/python
# -*- coding: iso-8859-1 -*-
'''
edfistat.py

Usage: see main()

'''

import os, sys, getopt, re, string, math #@UnresolvedImport
import collections, ConfigParser
import math

try:
    from thread import get_ident as _get_ident
except ImportError:
    from dummy_thread import get_ident as _get_ident

try:
    from _abcoll import KeysView, ValuesView, ItemsView
except ImportError:
    pass

class MyOrderedDict(dict):
    'Dictionary that remembers insertion order'
    # An inherited dict maps keys to values.
    # The inherited dict provides __getitem__, __len__, __contains__, and get.
    # The remaining methods are order-aware.
    # Big-O running times for all methods are the same as for regular dictionaries.

    # The internal self.__map dictionary maps keys to links in a doubly linked list.
    # The circular doubly linked list starts and ends with a sentinel element.
    # The sentinel element never gets deleted (this simplifies the algorithm).
    # Each link is stored as a list of length three:  [PREV, NEXT, KEY].

    def __init__(self, *args, **kwds):
        '''Initialize an ordered dictionary.  Signature is the same as for
        regular dictionaries, but keyword arguments are not recommended
        because their insertion order is arbitrary.

        '''
        if len(args) > 1:
            raise TypeError('expected at most 1 arguments, got %d' % len(args))
        try:
            self.__root
        except AttributeError:
            self.__root = root = []                     # sentinel node
            root[:] = [root, root, None]
            self.__map = {}
        self.__update(*args, **kwds)

    def __setitem__(self, key, value, dict_setitem=dict.__setitem__):
        'od.__setitem__(i, y) <==> od[i]=y'
        # Setting a new item creates a new link which goes at the end of the linked
        # list, and the inherited dictionary is updated with the new key/value pair.
        if key not in self:
            root = self.__root
            last = root[0]
            last[1] = root[0] = self.__map[key] = [last, root, key]
        dict_setitem(self, key, value)

    def __delitem__(self, key, dict_delitem=dict.__delitem__):
        'od.__delitem__(y) <==> del od[y]'
        # Deleting an existing item uses self.__map to find the link which is
        # then removed by updating the links in the predecessor and successor nodes.
        dict_delitem(self, key)
        link_prev, link_next, key = self.__map.pop(key)
        link_prev[1] = link_next
        link_next[0] = link_prev

    def __iter__(self):
        'od.__iter__() <==> iter(od)'
        root = self.__root
        curr = root[1]
        while curr is not root:
            yield curr[2]
            curr = curr[1]

    def __reversed__(self):
        'od.__reversed__() <==> reversed(od)'
        root = self.__root
        curr = root[0]
        while curr is not root:
            yield curr[2]
            curr = curr[0]

    def clear(self):
        'od.clear() -> None.  Remove all items from od.'
        try:
            for node in self.__map.itervalues():
                del node[:]
            root = self.__root
            root[:] = [root, root, None]
            self.__map.clear()
        except AttributeError:
            pass
        dict.clear(self)

    def popitem(self, last=True):
        '''od.popitem() -> (k, v), return and remove a (key, value) pair.
        Pairs are returned in LIFO order if last is true or FIFO order if false.

        '''
        if not self:
            raise KeyError('dictionary is empty')
        root = self.__root
        if last:
            link = root[0]
            link_prev = link[0]
            link_prev[1] = root
            root[0] = link_prev
        else:
            link = root[1]
            link_next = link[1]
            root[1] = link_next
            link_next[0] = root
        key = link[2]
        del self.__map[key]
        value = dict.pop(self, key)
        return key, value

    # -- the following methods do not depend on the internal structure --

    def keys(self):
        'od.keys() -> list of keys in od'
        return list(self)

    def values(self):
        'od.values() -> list of values in od'
        return [self[key] for key in self]

    def items(self):
        'od.items() -> list of (key, value) pairs in od'
        return [(key, self[key]) for key in self]

    def iterkeys(self):
        'od.iterkeys() -> an iterator over the keys in od'
        return iter(self)

    def itervalues(self):
        'od.itervalues -> an iterator over the values in od'
        for k in self:
            yield self[k]

    def iteritems(self):
        'od.iteritems -> an iterator over the (key, value) items in od'
        for k in self:
            yield (k, self[k])

    def update(*args, **kwds):
        '''od.update(E, **F) -> None.  Update od from dict/iterable E and F.

        If E is a dict instance, does:           for k in E: od[k] = E[k]
        If E has a .keys() method, does:         for k in E.keys(): od[k] = E[k]
        Or if E is an iterable of items, does:   for k, v in E: od[k] = v
        In either case, this is followed by:     for k, v in F.items(): od[k] = v

        '''
        if len(args) > 2:
            raise TypeError('update() takes at most 2 positional '
                            'arguments (%d given)' % (len(args),))
        elif not args:
            raise TypeError('update() takes at least 1 argument (0 given)')
        self = args[0]
        # Make progressively weaker assumptions about "other"
        other = ()
        if len(args) == 2:
            other = args[1]
        if isinstance(other, dict):
            for key in other:
                self[key] = other[key]
        elif hasattr(other, 'keys'):
            for key in other.keys():
                self[key] = other[key]
        else:
            for key, value in other:
                self[key] = value
        for key, value in kwds.items():
            self[key] = value

    __update = update  # let subclasses override update without breaking __init__

    __marker = object()

    def pop(self, key, default=__marker):
        '''od.pop(k[,d]) -> v, remove specified key and return the corresponding value.
        If key is not found, d is returned if given, otherwise KeyError is raised.

        '''
        if key in self:
            result = self[key]
            del self[key]
            return result
        if default is self.__marker:
            raise KeyError(key)
        return default

    def setdefault(self, key, default=None):
        'od.setdefault(k[,d]) -> od.get(k,d), also set od[k]=d if k not in od'
        if key in self:
            return self[key]
        self[key] = default
        return default

    def __repr__(self, _repr_running={}):
        'od.__repr__() <==> repr(od)'
        call_key = id(self), _get_ident()
        if call_key in _repr_running:
            return '...'
        _repr_running[call_key] = 1
        try:
            if not self:
                return '%s()' % (self.__class__.__name__,)
            return '%s(%r)' % (self.__class__.__name__, self.items())
        finally:
            del _repr_running[call_key]

    def __reduce__(self):
        'Return state information for pickling'
        items = [[k, self[k]] for k in self]
        inst_dict = vars(self).copy()
        for k in vars(MyOrderedDict()):
            inst_dict.pop(k, None)
        if inst_dict:
            return (self.__class__, (items,), inst_dict)
        return self.__class__, (items,)

    def copy(self):
        'od.copy() -> a shallow copy of od'
        return self.__class__(self)

    @classmethod
    def fromkeys(cls, iterable, value=None):
        '''OD.fromkeys(S[, v]) -> New ordered dictionary with keys from S
        and values equal to v (which defaults to None).

        '''
        d = cls()
        for key in iterable:
            d[key] = value
        return d

    def __eq__(self, other):
        '''od.__eq__(y) <==> od==y.  Comparison to another OD is order-sensitive
        while comparison to a regular mapping is order-insensitive.

        '''
        if isinstance(other, MyOrderedDict):
            return len(self)==len(other) and self.items() == other.items()
        return dict.__eq__(self, other)

    def __ne__(self, other):
        return not self == other

    # -- the following methods are only used in Python 2.7 --

    def viewkeys(self):
        "od.viewkeys() -> a set-like object providing a view on od's keys"
        return KeysView(self)

    def viewvalues(self):
        "od.viewvalues() -> an object providing a view on od's values"
        return ValuesView(self)

    def viewitems(self):
        "od.viewitems() -> a set-like object providing a view on od's items"
        return ItemsView(self)

'''
    GLOBAL VARIABLES AND DEFAULT CONFIGURATION
'''
VERSION = 'v1.0'

LF = os.linesep

LOG_LEVELS = {
     'critical'     :       1,
     'error'        :       2,
     'warning'      :       3,
     'info'         :       4,
     'debug'        :       5
}

CONF = {
    'LOG_LEVEL' : 'info',
}       
            
'''
    CLASSES
'''
        
class DataManager:
    
    @staticmethod
    def listStringToInt(list):
        intList = []
        for e in list:
            intList.append(int(e))
        return intList

    @staticmethod
    def listStringToFloat(list):
        floatList = []
        for e in list:
            floatList.append(float(e))
        return floatList

    @staticmethod
    def listsOp(listCollection, opHandler):
        list = []
        listLen = 0
        for l in listCollection:
            assert listLen == 0 or listLen == len(l)
            listLen = len(l)
        for i in range(listLen):
            sum = 0
            tmpList = []
            for l in listCollection:
                tmpList.append(l[i])
            list.append(opHandler(tmpList))
        return list

    @staticmethod
    def listsSum(listCollection):
        return DataManager.listsOp(listCollection, DataManager.listSum)

    @staticmethod
    def listsAvg(listCollection):
        return DataManager.listsOp(listCollection, DataManager.listAvg)

    @staticmethod
    def listsMin(listCollection):
        return DataManager.listsOp(listCollection, DataManager.listMin)

    @staticmethod
    def listsMax(listCollection):
        return DataManager.listsOp(listCollection, DataManager.listMax)

    @staticmethod
    def listsMedian(listCollection):
        return DataManager.listsOp(listCollection, DataManager.listMedian)

    @staticmethod
    def listsRelErr(listCollection):
        return DataManager.listsOp(listCollection, DataManager.listRelErr)

    @staticmethod
    def listsRelIncr(listCollection):
        return DataManager.listsOp(listCollection, DataManager.listRelIncr)

    @staticmethod
    def listsDiv(listCollection):
        return DataManager.listsOp(listCollection, DataManager.listDiv)

    @staticmethod
    def listsRSD(listCollection):
        return DataManager.listsOp(listCollection, DataManager.listRSD)

    @staticmethod
    def listsRSE(listCollection):
        return DataManager.listsOp(listCollection, DataManager.listsRSE)

    @staticmethod
    def listsVariance(listCollection):
        return DataManager.listsOp(listCollection, DataManager.listVariance)

    @staticmethod
    def listsStddev(listCollection):
        return DataManager.listsOp(listCollection, DataManager.listStddev)

    @staticmethod
    def listsAggr(listCollection):
        return DataManager.listsOp(listCollection, DataManager.listNoop)

    @staticmethod
    def listsAppend(listCollection):
    	appendedList = []
    	for l in listCollection:
    		appendedList.extend(l)
        return appendedList

    @staticmethod
    def listsProd(listCollection):
        list = []
        listLen = 0
        for l in listCollection:
            assert listLen == 0 or listLen == len(l)
            listLen = len(l)
        for i in range(listLen):
            prod = 0
            for l in listCollection:
                prod *= l[i]
            list.append(prod)
        return list

    @staticmethod
    def listMax(list):
        assert len(list) > 0
        max = list[0]
        for e in list:
            if e > max:
                max = e
        return max

    @staticmethod
    def listMin(list):
        assert len(list) > 0
        min = list[0]
        for e in list:
            if e < min:
                min = e
        return min

    @staticmethod
    def listSum(list):
        sum = 0
        for l in list:
            sum += l
        return sum

    @staticmethod
    def listAvg(list):
        assert len(list) > 0
        return float(DataManager.listSum(list))/float(len(list))

    @staticmethod
    def listMedian(list):
        myList = sorted(list)
        if len(myList) % 2 == 1:
            return myList[(len(myList)+1)/2-1]
        else:
            lower = myList[len(myList)/2-1]
            upper = myList[len(myList)/2]
            return (float(lower + upper)) / 2

    @staticmethod
    def listRelErr(list):
    	return math.fabs(DataManager.listRelIncr(list))

    @staticmethod
    def listRelIncr(list):
        assert len(list) == 2
        if list[0] == 0:
            base = 0.001
        else:
            base = list[0]
        return float(list[1] - list[0]) / float(base)

    @staticmethod
    def listDiv(list):
        assert len(list) == 2
        if list[0] == 0:
            return 0
        return float(list[0]) / float(list[1])

    @staticmethod
    def listRSD(list):
    	average = DataManager.listAvg(list)
    	if average == 0:
            return 0
    	return math.fabs(DataManager.listStddev(list)/average)

    @staticmethod
    def listStderr(list):
    	listLen = len(list)
    	if listLen == 0:
            return 0
    	return DataManager.listStddev(list)/math.sqrt(listLen)

    @staticmethod
    def listRSE(list):
    	average = DataManager.listAvg(list)
    	if average == 0:
            return 0
        return DataManager.listStderr(list)/average

    @staticmethod
    def listCovariance(list1, list2):
        assert len(list1) == len(list2)
        avgList1 = DataManager.listAvg(list1)
        avgList2 = DataManager.listAvg(list2)
        covar = 0
        for i in range(len(list1)):
            x1 = list1[i] - avgList1
            x2 = list2[i] - avgList2
            covar += x1*x2
        covar = float(covar) / float(len(list1))
        return covar

    @staticmethod
    def listVariance(list):
        return DataManager.listCovariance(list, list)

    @staticmethod
    def listStddev(list):
        var = DataManager.listVariance(list)
        stddev = math.sqrt(var)
        return stddev

    @staticmethod
    def listPCC(list1, list2):
        covar = DataManager.listCovariance(list1, list2)
        stddevList1 = DataManager.listStddev(list1)
        stddevList2 = DataManager.listStddev(list2)
        stddevProd = stddevList1*stddevList2
        if stddevProd == 0:
            return 0
        pcc = float(covar)/float(stddevProd)
        return pcc

    @staticmethod
    def listNoop(list):
        return list

class ActionManager:

    def __init__(self, isSingle, action):
    	self.isSingle = isSingle
        singleActionDictionary = {
            'median'		:	DataManager.listMedian,
            'average'		:	DataManager.listAvg,
            'var'		:	DataManager.listVariance,
            'stddev'		:	DataManager.listStddev,
            'min'		:	DataManager.listMin,
            'max'		:	DataManager.listMax,
            'sum'		:	DataManager.listSum
        }
        multiActionDictionary = {
            'median'		:	DataManager.listsMedian,
            'average'		:	DataManager.listsAvg,
            'var'		:	DataManager.listsVariance,
            'stddev'		:	DataManager.listsStddev,
            'min'		:	DataManager.listsMin,
            'max'		:	DataManager.listsMax,
            'sum'		:	DataManager.listsSum,
            'relerr'		:	DataManager.listsRelErr,
            'relincr'		:	DataManager.listsRelIncr,
            'div'		:	DataManager.listsDiv,
            'rsd'		:	DataManager.listsRSD,
            'rse'		:	DataManager.listsRSE,
            'append'		:	DataManager.listsAppend,
            'csv'		:	DataManager.listsAggr,
            'matlab'		:	DataManager.listsAggr,
            'root'		:	DataManager.listsAggr,
            'rootm'		:	DataManager.listsAggr,
        }
        if self.isSingle:
            self.actionInit(singleActionDictionary, action)
        else:
            self.actionInit(multiActionDictionary, action)

    def actionInit(self, actionDictionary, action):
        if not action in actionDictionary:
            raise Exception('Bad action: ' + action)
        self.action = actionDictionary[action]

    def doAction(self, myLists):
    	if self.isSingle:
    	    list = []
            list.append(self.action(myLists[0]))
            return list
        else:
            return self.action(myLists)

class ConfigManager:

    @staticmethod
    def getConfigObj(file, section):
    	try:
            eval('config = ConfigParser.RawConfigParser(None,MyOrderedDict,False)')
    	except SyntaxError:
    	    config = ConfigParser.RawConfigParser(None,MyOrderedDict)
    	return config

    @staticmethod
    def configToItems(file, section):
    	config = ConfigManager.getConfigObj(file, section)
        if not config.read(file):
            raise Exception('Bad filename: ' + file)
        if not config.has_section(section):
            raise Exception('Bad section: ' + section)
        keys = []
        values = []
        for o in config.options(section):
            value = config.getfloat(section, o)
            keys.append(o)
            values.append(value)
        return (keys, values)

    @staticmethod
    def itemsToConfig(items, section, output):
        config = ConfigManager.getConfigObj(file, section)
        config.add_section(section)
        (keys, values) = items
        for i in range(len(keys)):
            config.set(section, keys[i], values[i])
        config.write(output)

    @staticmethod
    def itemsToCSV(items, section, output):
    	output.write('# %s%s' % (section, LF))
    	(keys, values) = items
    	for i in range(len(keys)):
            output.write('%s' % keys[i])
            for v in values[i]:
                output.write(';%s' % v)
            output.write(LF)

    @staticmethod
    def itemsToMatlab(items, section, output):
    	output.write('%% %s%s' % (section, LF))
    	(keys, values) = items
    	for i in range(len(keys)):
            output.write('%s = [' % keys[i])
            for v in values[i]:
                output.write(' %s' % v)
            output.write(' ]%s' % LF)

    @staticmethod
    def itemsToRoot(items, section, output):
    	(keys, values) = items
    	for i in range(len(keys)):
            for v in values[i]:
                output.write('%s%s' % (v , LF))
            output.write('%s' % LF)

    @staticmethod
    def itemsToRootm(items, section, output):
    	(keys, values) = items
    	dict = {}
    	for j in range(len(values[0])):
    	    dict[j] = []
    	for i in range(len(keys)):
    	    for j in range(len(values[i])):
    	        dict[j].append(values[i][j])
    	for k,values in sorted(dict.items()):
            for v in values:
                output.write('%s ' % v)
            output.write('%s' % LF)

    @staticmethod
    def itemsToOutput(outputFormat, items, section, output):
    	if outputFormat == 'csv':
            ConfigManager.itemsToCSV(items, section, output)
    	elif outputFormat == 'matlab':
            ConfigManager.itemsToMatlab(items, section, output)
    	elif outputFormat == 'root':
            ConfigManager.itemsToRoot(items, section, output)
    	elif outputFormat == 'rootm':
            ConfigManager.itemsToRootm(items, section, output)
    	else:
            ConfigManager.itemsToConfig(items, section, output)

'''
    FUNCTIONS
'''
def log(level, line, isFormatted=1):
    global LF, LOG_LEVELS, CONF
    
    #default to error level if something goes wrong
    if level not in LOG_LEVELS:
        level = 'error'
    
    #print if the specified level is included by configuration    
    if LOG_LEVELS[level] <= LOG_LEVELS[CONF['LOG_LEVEL']]:
        if isFormatted:
            line = '[%s] %s%s' % (level, line, LF)
        else:
            line = '%s%s' % (line, LF)
        sys.stderr.write(line)

def main(argv):
    global LF, CONF, VERSION

    if len(argv) != 4:
    	sys.stderr.write('Usage: %s <output_section> <file:///path/to/files_sections_by_line.file | file1@section1:file2@section2:...:fileN@sectionN> <median|average|var|stddev|min|max|sum|relerr|relincr|div|rsd|rse|append|csv|matlab|root|rootm> %s' % (argv[0], LF))
        sys.exit(1)
    outputSection = argv[1]
    filesString = argv[2]
    action = argv[3]

    # see if we have to read the input files from a config file
    m = re.match(r"file://(.*)", filesString)
    if m is not None:
        configFile = m.group(1)
        configFile = open(configFile, "r")
	files = [line.strip() for line in configFile]
    else:
        files = filesString.split(':')

    # parse input files and extract keys and values
    keysList = []
    valuesList = []
    assert(len(files) > 0)
    for f in files:
    	tokens = f.split('@')
    	if len(tokens) != 2:
    	    raise Exception('Invalid file|section name: ' + f)
    	file = tokens[0]
    	section = tokens[1]
        items = ConfigManager.configToItems(file, section)
        (keys, values) = items
        keysList.append(keys)
        valuesList.append(values)

    # perform action
    isSingle = len(keysList) == 1
    actionManager = ActionManager(isSingle, action)
    outputValues = actionManager.doAction(valuesList)
    if isSingle:
        outputKeys = [ action ]
    elif action == 'append':
    	outputKeys = []
    	for keys in keysList:
    	    outputKeys.extend(keys)
    else:
        outputKeys = keysList[0]

    # output results on stdout
    outputItems = (outputKeys, outputValues)
    ConfigManager.itemsToOutput(action, outputItems, outputSection, sys.stdout)

    #exit
    log('debug', 'Done')
    sys.exit(0)

#ENTRY POINT
if __name__ == "__main__":
    sys.exit(main(sys.argv))