-- DESCRIPTION: Verilator: a std.textio file builds a memory image at elaboration
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;

entity t is
  port (
    o0 : out std_logic_vector(7 downto 0);
    o1 : out std_logic_vector(7 downto 0)
  );
end entity t;

architecture rtl of t is
  type image_t is array (0 to 1) of std_logic_vector(7 downto 0);

  -- hread fills as many hex digits as the target is wide, so the "01" of the
  -- second line is left unread
  impure function load_image(path : string) return image_t is
    file img_file : text open read_mode is path;
    variable img_line : line;
    variable word : std_logic_vector(7 downto 0);
    variable image : image_t := (others => (others => '0'));
  begin
    for i in 0 to 1 loop
      exit when endfile(img_file);
      readline(img_file, img_line);
      hread(img_line, word);
      image(i) := word;
    end loop;
    return image;
  end function load_image;

  -- The image is read while the design is elaborated, not while it runs
  signal mem : image_t := load_image("t/t_vhdl_file_init.dat");
begin
  o0 <= mem(0);
  o1 <= mem(1);
end architecture rtl;
