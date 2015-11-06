require 'fileutils'

assets_base_dir = File.dirname File.expand_path __FILE__

FileUtils.mkdir_p "#{assets_base_dir}/build"

assets = File.new("#{assets_base_dir}/build/crattlecrute.assets", 'wb')
header = File.new("#{assets_base_dir}/src/assets.h", 'w')

header.write("// Generated #{Time.now}\n\n")

current_offset = 0

Dir.glob("#{assets_base_dir}/assets/**/*").each do |file|
  next if File.directory? file

  bytes = IO.binread(file)
  assets.write(bytes)

  puts file.gsub! /^.*[\/\\]assets[\/\\]/, ''
  file_ident = file.gsub(/[\.\s\?!\/\\-]/, '_').upcase

  header.write(
    "// #{file}\n"\
    "const static int #{file_ident}_OFFSET = #{current_offset};\n"\
    "const static int #{file_ident}_SIZE = #{bytes.size};\n"
  )

  current_offset += bytes.size
end

assets.close
header.close
