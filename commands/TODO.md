* Garantir que a saída de msg.payload.decode() realmente é uma string com os dados separados com vírgula ou mudar o tratamento caso não seja

* Realizar conexão com os brokers

* Adicionar nome do banco de dados monitorado em FILE_PATTERNS e modificar a extensão dos arquivos que serão adicionados no diretório commands

* As mensagens de log vêm todas misturadas no mesmo topic; precisa ver se a mensagem é "ACCESS", "ACCESS/BOOT", "SYSTEM" ou "SYSTEM/BOOT" e gravar no lugar certo
