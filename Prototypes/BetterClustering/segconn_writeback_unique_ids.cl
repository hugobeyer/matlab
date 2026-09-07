#bind layer &id int
#bind layer srcid int

@KERNEL
{
    global int *rawdata = (global int *) @srcid.data;
    int id = @id;
    if (id >= 0)
    {
        @id.set(rawdata[id]);
    }
}
