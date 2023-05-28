* Acertar os tipos de dados de cada coluna do banco de dados dos logs e se o tamanho dos tipos "char" são suficientes

* Garantir que a saída de msg.payload.decode() realmente é uma string com os dados separados com vírgula ou mudar o tratamento caso não seja

* Realizar conexão com os brokers

* Adicionar nome do banco de dados monitorado em FILE_PATTERNS e modificar a extensão dos arquivos que serão adicionados no diretório commands

* Verificar se a unicidade das chaves primárias adicionadas é suficiente para excluir erro do nodeMCU enviando uma mensagem mais de uma vez ou se é melhor adicionar unicidade para toda a linha