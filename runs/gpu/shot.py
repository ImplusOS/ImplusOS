import socket,time,sys,subprocess,os
d=sys.argv[1]; name=sys.argv[2]
s=socket.socket(socket.AF_UNIX);s.connect(d+'/mon.sock');time.sleep(0.3);s.recv(65536)
s.send(('screendump %s/%s.ppm\n'%(os.path.abspath(d),name)).encode());time.sleep(2);s.close()
subprocess.run(['convert','%s/%s.ppm'%(d,name),'%s/%s.png'%(d,name)])
