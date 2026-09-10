#bind layer !&gain
#bind layer !&shift

#bind layer average float
#bind layer min float
#bind layer max float 

#bind parm black float val=0
#bind parm white float val=1
#bind parm goalavg float val=0.5
#bind parm mode int
#bind parm doscale int val=1


@KERNEL
{
    float gain = 1.0f;
    float shift = 0.0f;

    if (@mode == 0) // Stretch
    {
        if (@max > @min)
        {
            gain = (@white-@black) / (@max - @min);
            shift = -@min * gain + @black;
        }    
        else
        {
            gain = 0;
            shift = (@white + @black)*0.5f;
        }
    }
    else if (@mode == 1) // min
    {
        if (@doscale)
        {
            if (@min)
            {
                gain = @black / @min;
            }
        }
        else
        {
            shift = @black - @min;
        }
    }
    else if (@mode == 2) // max
    {
        if (@doscale)
        {
            if (@max)
            {
                gain = @white / @max;
            }
        }
        else
        {
            shift = @white - @max;
        }
    }
    else if (@mode == 3) // avg
    {
        if (@doscale)
        {
            if (@average)
            {
                gain = @goalavg / @average;
            }
        }
        else
        {
            shift = @goalavg - @average;
        }
    }
    else if (@mode == 4) // avg
    {
        float scale = max(fabs(@min), fabs(@max));
        if (scale)
            gain = @white / scale;
    }
    @gain.set(gain);
    @shift.set(shift);
}
