# DESCRIPTION
#	Facilitate uploading files to a board
#
# USAGE
#	make host=192.168.7.159 target...
#
# PARAMETERS
#
#	host - (required) the Wemos D1 mini board to which a file should 
#		be uploaded
#
# EXAMPLES
#
#	make host=192.168.7.159 upload-ui
#
#		Uploads all html and JavaScript files to the board with the IP
#		address 192.168.7.159
#
# NOTES
#
#	Known IP addresses:
#
#		192.168.7.122	sprinklers.local (aka, "production")
#		192.168.7.65	sptest.local (aka, "test bed")
#		192.168.7.159	sp3.local
#
# TO DO
#
#	- add in commands to upload an existing firmware file to a host

ifndef host
$(error host not set)
endif

upload-html:
	curl http://$(host)/upload -F 'name=@data/index.html'

upload-js:
	curl http://$(host)/upload -F 'name=@data/sprinklers.js'

upload-ui: upload-html upload-js