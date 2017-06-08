import csv

csvfile = "csvlena.csv"
row0_val = 25
row1_val = 5295
count = 0



with open(csvfile, 'rU') as file:
	reader = csv.reader(file, lineterminator='\n')
	row0 = []
	row1 = []
	row2 = []
	row3 = []
	row4 = []
	row5 = []


	for row in reader:
		if(count>=24 and count<=2645):
			row0.append(row)
		if(count>=2652 and count<=5273):
			row0.append(row)		
		if(count>=5280 and count<=7901):
			row0.append(row)
		if(count>=7922 and count<=10543):
			row1.append(row)
		if(count>=10550 and count<=13171):
			row1.append(row)
		if(count>=13178 and count<=15799):
			row1.append(row)

		count = count+1;

	for i in xrange(1,7862):
		#print(str(row0[i]==row1[i]) + "     "+str(i)+str(row0[i])+str(row0[i]))
		print( str(i) +" : "+ str(row0[i]==row1[i]) )
		#print(row0[i])
		#print(row1[i])
		if not(row0[i]==row1[i]):
			print("FALSE 	"+str(i))
			print(row0[i])
			print(row1[i])

			print(row0[i+1])
			print(row1[i+1])
			print(row0[i+2])
			print(row1[i+2])
			break


	#print(str(row0 == row1) + " - " + str(val) ) 


