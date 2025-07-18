// #include "elf_loader.hh"

// #include <stdint.h>

// #include "elf.h"
// #include "vector.hh"
// #include "map.hh"

// void * load_elf(char* fn, EmbExt2 *fs_context)
// {
//   void *fd = context->open(fn, O_RDONLY, 0777);
//   struct fs_stat s;
//   if (fd == -1)
//       panic("Elf can't be opened");
//   if (fs_context->fstat(fd, &s) < 0)
//       panic("Elf can't be statted");
//   size_t size = s.st_size;

//   char* buf = (char *)malloc(size);
//   char* exec = (char *)malloc(size);
//   uint64_t load_offset = (uint64_t) exec;
//   if (!buf)
//       panic("Elf can't be loaded into memory");
//   fs_context->close(fd);

//   assert(size >= sizeof(Elf64_Ehdr));
//   const Elf64_Ehdr* eh64 = (const Elf64_Ehdr*)buf;
//   assert(IS_ELF32(*eh64) || IS_ELF64(*eh64));
//   assert(IS_ELFLE(*eh64) || IS_ELFBE(*eh64));
//   assert(IS_ELF_EXEC(*eh64) || IS_ELF_DYN(*eh64));
//   assert(IS_ELF_RISCV(*eh64) || IS_ELF_EM_NONE(*eh64));
//   assert(IS_ELF_VCURRENT(*eh64));

//   if (IS_ELF_EXEC(*eh64)) {
//     load_offset = 0;
//   }

//   Vector<uint8_t> zeros;
//   Map<char*, uint64_t> symbols;
                                                                        
//   ehdr_t* eh = (ehdr_t*)buf;                                                 
//   phdr_t* ph = (phdr_t*)(buf + eh->e_phoff);                          
//   *entry = eh->e_entry + load_offset;                                 
//   assert(size >= eh->e_phoff + eh->e_phnum * sizeof(*ph));     
//   for (unsigned i = 0; i < eh->e_phnum; i++) {                        
//     if (ph[i].p_type == PT_LOAD && ph[i].p_memsz) {            
//       uint8_t* load_addr = (uint8_t*)(ph[i].p_paddr + load_offset);                  
//       if (ph[i].p_filesz) {                                           
//         assert(size >= ph[i].p_offset + ph[i].p_filesz);       
//         memcpy(load_addr, ph[i].p_filesz, (uint8_t*)buf + ph[i].p_offset);                 
//       }                                                                      
//       if (size_t pad = ph[i].p_memsz - ph[i].p_filesz) {       
//         zeros.resize(pad);                                                   
//         memcpy(load_addr + ph[i].p_filesz, pad, zeros.data());  
//       }                                                                      
//     }                                                                        
//   }                                                                          
//   shdr_t* sh = (shdr_t*)(buf + eh->e_shoff);                          
//   assert(size >= eh->e_shoff + eh->e_shnum * sizeof(*sh));     
//   assert(eh->e_shstrndx < eh->e_shnum);                        
//   assert(size >= sh[eh->e_shstrndx].sh_offset +                
//                       sh[eh->e_shstrndx].sh_size);              
//   char* shstrtab = buf + sh[eh->e_shstrndx].sh_offset;         
//   unsigned strtabidx = 0, symtabidx = 0;                                     
//   for (unsigned i = 0; i < eh->e_shnum; i++) {                        
//     unsigned max_len =                                                       
//         sh[eh->e_shstrndx].sh_size - sh[i].sh_name;     
//     assert(sh[i].sh_name < sh[eh->e_shstrndx].sh_size); 
//     assert(strnlen(shstrtab + sh[i].sh_name, max_len) < max_len);     
//     if (sh[i].sh_type & SHT_NOBITS) continue;                         
//     assert(size >= sh[i].sh_offset + sh[i].sh_size);           
//     if (strcmp(shstrtab + sh[i].sh_name, ".strtab") == 0)             
//       strtabidx = i;                                                         
//     if (strcmp(shstrtab + sh[i].sh_name, ".symtab") == 0)             
//       symtabidx = i;                                                         
//   }                                                                          
//   if (strtabidx && symtabidx) {                                              
//     char* strtab = buf + sh[strtabidx].sh_offset;                     
//     sym_t* sym = (sym_t*)(buf + sh[symtabidx].sh_offset);             
//     for (unsigned i = 0; i < sh[symtabidx].sh_size / sizeof(sym_t);   
//           i++) {                                                              
//       unsigned max_len =                                                     
//           sh[strtabidx].sh_size - sym[i].st_name;              
//       assert(sym[i].st_name < sh[strtabidx].sh_size);          
//       assert(strnlen(strtab + sym[i].st_name, max_len) < max_len);    
//       symbols[strtab + sym[i].st_name] = sym[i].st_value + load_offset;      
//     }                                                                        
//   }                                                                          
 
//   Map<uint64_t, char*> addr2symbol;
//   for (auto i : symbols) {
//     auto it = addr2symbol.find(i.second);
//     if ( it == addr2symbol.end())
//       addr2symbol[i.second] = i.first;
//   }
//   free(buff);

// }