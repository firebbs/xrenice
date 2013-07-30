xrenice
================================

xrenice - utility alters the scheduling priority of one or more running processes by its X resource.
<pre>
You can choose window by cursor (like xkill or xprop) to change priority.
For unpriveledged users, to set set priority less than zero, xrenice can be used with su, sudo or SUID/SGID
usage:  xrenice [-p priority]
or
[XRENICEPRIO=priority] xrenice
By default program using highest priority: -20
You can set custom priority using enviroment XRENICEPRIO or argument -p.
Arguments:
    -p priority                         set custom priority
    -g                                  get priority
</pre>