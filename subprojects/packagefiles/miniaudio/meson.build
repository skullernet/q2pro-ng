project('miniaudio', 'c', version: '0.11.22', license: 'mit')

args = [
  '-DMA_USE_STDINT',
  '-DMA_NO_ENCODING',
  '-DMA_NO_DECODING',
  '-DMA_NO_RESOURCE_MANAGER',
  '-DMA_NO_GENERATION',
]

miniaudio = static_library('miniaudio', 'miniaudio.c', c_args: args)

miniaudio_dep = declare_dependency(
  compile_args:        args,
  include_directories: '.',
  link_with:           miniaudio,
)
