#bind layer &dst int
#bind layer !&alpha float
#bind layer mask float

#bind parm above int
#bind parm threshold float
#bind parm offset float

#define STACK 5
static int
_findClass(global int *data, int ptidx)
{
    int stack[STACK];
    int stacksize = 0;
    int newclass = -1;
    while (1)
    {
        newclass = data[ptidx];
        if (newclass == -1) break;
        if (newclass == ptidx) break;
        
        if (stacksize < STACK)
            stack[stacksize++] = ptidx;
        ptidx = newclass;
    }
    
    // Write out the stack, if any..
    while (stacksize--)
    {
        // this is safe with contention, we are
        // consistent in the result and we are never
        // re-rooting here, just changing the path
        // through an existing tree, so it won't
        // cause splits.
        atomic_min(&data[stack[stacksize]], newclass);
    }
    return newclass;
}

static void
_disjointUnion(global int *data, int a, int b)
{
    // No-op if illegal
    if (a < 0 || b < 0)
        return;
  
    while (1)
    {
        int abase = _findClass(data, a);
        int bbase = _findClass(data, b);
        if (abase < 0 || bbase < 0)
            return;
    
        if (abase == bbase)
            return;
        
        int newval, newidx;
        newidx = max(abase, bbase);
        newval = min(abase, bbase);
        
        // At this point data[newidx] == newidx should
        // be true as newidx must be a current root.
        // If someone re-roots it while working,
        // we can't update.
        int dataval = atomic_cmpxchg(&data[newidx], newidx, newval);
        if (dataval == newidx)
            break;
    }
}

static int _computelevel(float val, float thresh, float off)
{
    int result;
    val -= off;
    if (thresh)
        val /= thresh;
    else
    {
        // reinterpret float to int so 1ulps changes..
        result = *((int *)&val);
        return result;
    }
    return floor(val);
}

@KERNEL
{
    int2 xy = (int2)(@ix, @iy);
    
    global int *rawdata = @dst.data;
    
    int a = @dst;
    
    if (a < 0)
        return;
        
    float thisside;
    int   thislevel;
    if (@above == 2)
        thisside = @mask.bufferIndex(xy) - @threshold;
    else if (@above == 3)
        thislevel = _computelevel(@mask.bufferIndex(xy), @threshold, @offset);
        
    xy.x--;
    // Allow wrapping
    if (@dst.border == IMX_WRAP || xy.x >= 0)
    {
        int b = @dst.bufferIndex(xy);
        
        if (@above == 2)
        {
            float otherside = @mask.bufferIndex(xy) - @threshold;
            if (signbit(otherside) == signbit(thisside))
                _disjointUnion(rawdata, a, b);
        }
        else if (@above == 3)
        {
            int otherlevel = _computelevel(@mask.bufferIndex(xy), @threshold, @offset);
            if (thislevel == otherlevel)
                _disjointUnion(rawdata, a, b);
        }
        else
            _disjointUnion(rawdata, a, b);
    }
    xy.x++;
    xy.y--;
    // Allow wrapping
    if (@dst.border == IMX_WRAP || xy.y >= 0)
    {
        int b = @dst.bufferIndex(xy);
        if (@above == 2)
        {
            float otherside = @mask.bufferIndex(xy) - @threshold;
            if (signbit(otherside) == signbit(thisside))
                _disjointUnion(rawdata, a, b);
        }
        else if (@above == 3)
        {
            int otherlevel = _computelevel(@mask.bufferIndex(xy), @threshold, @offset);
            if (thislevel == otherlevel)
                _disjointUnion(rawdata, a, b);
        }
        else
            _disjointUnion(rawdata, a, b);
    }
    xy.y++;
    
    // Dst was written to by raw buffers.
}


@WRITEBACK
{
    int2 xy = (int2)(@ix, @iy);
    
    global int *rawdata = @dst.data;
    
    int a = @dst;
    
    if (a < 0)
    {
        @alpha.set(0);
        return;
    }
        
    int dstidx = @ix + @iy * @xres;
    
    a = @dst;
    a = _findClass(rawdata, a);
    atomic_min(&rawdata[dstidx], a);
    
    a = ((global volatile int *)rawdata)[dstidx];
    if (a < 0)
        @alpha.set(0);
    else
        @alpha.set(1);
}
