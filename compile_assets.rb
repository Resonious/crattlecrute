require 'fileutils'

FileUtils.mkdir_p 'build'

assets = File.new("build/crattlecrute.assets", 'wb')

header = File.new("src/assets.h", 'w')
header.write("// Generated #{Time.now}\n\n")

current_offset = 0

Dir.glob("./assets/**/*").each do |file|
  next if File.directory? file

  bytes = IO.binread(file)
  assets.write(bytes)

  puts file.gsub! /^\.\/assets\//, ''
  file_ident = file.gsub(/[\.\s\?!\/\\-]/, '_').upcase

  header.write(
    "const int #{file_ident}_OFFSET = #{current_offset};\n"\
    "const int #{file_ident}_SIZE = #{bytes.size};\n"
  )

  current_offset += bytes.size
end

assets.close
header.close
