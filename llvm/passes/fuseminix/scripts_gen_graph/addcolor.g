BEGIN {
}
N{
	if ( 1 == match($.label, "mx_pm"))
	{
		$.color="bisque";
		$.style="filled";
	}
	if ( 1 == match($.label, "mx_vfs"))
	{
		$.color="dodgerblue";
		$.style="filled";
	}
	if ( 1 == match($.label, "mx_rs"))
	{
		$.color="brown1";
		$.style="filled";
	}
	if ( 1 == match($.label, "mx_mem"))
	{
		$.color="burlywood1";
		$.style="filled";
	}
	if ( 1 == match($.label, "mx_sched"))
	{
		$.color="cadetblue1";
		$.style="filled";
	}
	if ( 1 == match($.label, "mx_tty"))
	{
		$.color="chocolate1";
		$.style="filled";
	}
	if ( 1 == match($.label, "mx_ds"))
	{
		$.color="cornflowerblue";
		$.style="filled";
	}
	if ( 1 == match($.label, "mx_mfs"))
	{
		$.color="cyan";
		$.style="filled";
	}
	if ( 1 == match($.label, "mx_vm"))
	{
		$.color="darkgoldenrod1";
		$.style="filled";
	}
	if ( 1 == match($.label, "mx_pfs"))
	{
		$.color="darkolivegreen2";
		$.style="filled";
	}
	if ( 1 == match($.label, "mx_input"))
	{
		$.color="darkorange";
		$.style="filled";
	}
	if ( 1 == match($.label, "mx_is"))
	{
		$.color="darkseagreen1";
		$.style="filled";
	}
	if ( 1 == match($.label, "mx_ipc"))
	{
		$.color="firebrick1";
		$.style="filled";
	}
	if ( 1 == match($.label, "mx_devman"))
	{
		$.color="forestgreen";
		$.style="filled";
	}
	if ( $.label == "")
	{
		int fd = openF("nodes.prune", "a");
		printf(fd, "%s\n", $.name);
		closeF(fd);
	}
}
END_G {
        $O = $;
}

