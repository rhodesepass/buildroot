# if we are not running in /dev/tty0, skip animation
# if [ "$(tty)" != "/dev/tty0" ]; then
#     echo "Welcome to Rhodes Island Pass Debug Shell!"
#     echo "You are in Terminal $(tty)."
#     return
# fi

# if epass_drm_app is not present
if [ ! -f "./epass_drm_app" ]; then
cat << EOF
  _   _  ____  _____       _______       
 | \ | |/ __ \|  __ \   /\|__   __|/\    
 |  \| | |  | | |  | | /  \  | |  /  \   
 | . \ | |  | | |  | |/ /\ \ | | / /\ \  
 | |\  | |__| | |__| / ____ \| |/ ____ \ 
 |_| \_|\____/|_____/_/    \_\_/_/    \_\
                                         
Please copy 'epass_drm_app' and asset files.
to app directory.
EOF

    return
fi

cat << EOF                                        
                      *                      
                     -.:@                    
                    :.-..*                   
                   :.:.:..%                  
                 #..:   ...+                 
                %..:........#                
               *..%@:=@+:%%. =               
              *. -@@##@%%@@= .+              
             -.  =@@@@@@@@@+   :             
            -... -%#+*#@@@%- .:.:            
           -...   .%@@@@@%.   ...:@          
          :.:.    :@@@@@@@-    ...:%         
        @:..     .#@@@@@@@%.     ...#        
       #:...     -@%=%@@@#%-     ....+       
      #....   ..-+-:-*@@@@@#-..    ...+      
     +...    .=@#..@@@@@@@@@@@-.    ...=     
    *.... .::-------------------::.. .:.=    
   -...=..-=--+-.=::#:=.+-::-*-=*.-..=...:   
  *.......................................- 

EOF
sleep 2
cat << EOF
---------------------------------------------
     RHODES ISLAND AUTHORIZATION PASS
   VERSION 1.0 (c) Ada.Closure.Church 1097
---------------------------------------------
EOF
echo -n -e "\e[31m"
cat << EOF
   This pass certifies that the bearer is an
 authorized operator of Rhodes Island Co'Ltd.
  Unauthorized use of Rhodes Island property
            is strictly prohibited.
EOF
echo -n -e "\e[0m"
cat << EOF
---------------------------------------------
EOF
sleep 2
echo -n -e "\e[32m"
echo "Welcome to Rhodes Island!"
echo "You are in Terminal $(tty)."
echo "Access Level: Operator"
echo -n -e "\e[0m"
echo ""
echo -n "System initializing"
for i in $(seq 1 5); do
    echo -n "."
    sleep 1
done
echo ""
echo "Starting Application..."
ls /sys/bus/iio/devices/

sleep 1
# echo 0 > /sys/class/vtconsole/vtcon1/bind
# ./lvglsim
chmod +x ./epass_drm_app
./epass_drm_app
