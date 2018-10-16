for number in 50 100 150 200; do
    for algo in rand fifo lru clock opt; do
	for trf in tr-simpleloop.ref tr-matmul.ref tr-blocked.ref tr-custom.ref; do
	    echo ""
	    echo "File name:$trf-$algo-$number"
	    echo ""
	    (>&2 echo ./sim -f $trf -m $number -a $algo -s 32000)
	    ./sim -f $trf -m $number -a $algo -s 32000 | egrep "count|eviction|references|rate"
	    echo ""
		done
		done
done > columns.txt
		   
	
	
