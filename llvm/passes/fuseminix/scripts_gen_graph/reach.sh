# reach.sh
NODE=$1
INGRAPH=$2
OUTFILE=$3
G_LABEL=$4

if [ $# -ge 3 ]
then
  OUTFILE=$3
else
  OUTFILE="reach.out"
fi
echo node: $NODE
echo ingraph : $INGRAPH
echo OUTFILE: $OUTFILE

gvpr -f reach.g -a"$NODE 0" $INGRAPH > tmpf
gvpr -f reach.g -a"$NODE" $INGRAPH >> tmpf
sort -u tmpf > nodes
gvpr -f select.g -a"$G_LABEL" $INGRAPH > tmpf
cat tmpf > $OUTFILE
end_node=`tail -n 5 $OUTFILE | grep "Node[0-9a-z]*" | tail -n 1 | cut -d\> -f 2 | tr -d \;`
echo endnode: $end_node
gvpr -f prune.g -a"$end_node" $OUTFILE > tmpf
if [ -f "labels.prune" ]
then
	while read line
	do
		del_nodes=`grep "label=\"{.*$line.*}\"" tmpf | grep -o "Node[0-9a-z]*" | uniq`
		for del_node in $del_nodes
		do
			echo "Pruning node: $del_node [ $line ]"
			if [[ "$del_node" == "" ]]
			then
				continue
			fi
			gvpr -f prune.g -a"$del_node" tmpf > tmpf.p
			mv tmpf.p tmpf
		done
	done < "labels.prune"
fi

touch "nodes.prune"
gvpr -f addcolor.g tmpf > tmpf.color
mv tmpf.color tmpf

if [ -f "nodes.prune" ]
then
	while read line
	do
		echo "Pruning node: $line"
		if [[ "$line" == "" ]]
		then
			continue
		fi
		gvpr -f prune.g -a"$line" tmpf > tmpf.p
		mv tmpf.p tmpf
	done < "nodes.prune"
	rm -f "nodes.prune" 2>/dev/null
fi
cat tmpf > $OUTFILE

# cleanup
rm tmpf 2>/dev/null
rm nodes 2>/dev/null
