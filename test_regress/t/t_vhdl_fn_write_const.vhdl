-- DESCRIPTION: Verilator: a function filling a 1024-element local that folds to a constant
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Each element write lands in the constant local in place, so the budget on rebuilt values does not apply

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity t is end entity;
architecture a of t is
  type word_t is array (0 to 1023) of std_ulogic_vector(15 downto 0);
  type img_t is array (0 to 1023) of integer;
  function mk(n : integer) return img_t is
    variable r : img_t;
  begin
    for i in 0 to 1023 loop r(i) := (i * 37 + n) mod 65536; end loop;
    return r;
  end function;
  constant img : img_t := mk(5);
  function init_rom(init : img_t) return word_t is
    variable m : word_t;
  begin
    for i in 0 to 1023 loop
      m(i) := std_ulogic_vector(to_unsigned(init(i), 16));
    end loop;
    return m;
  end function;
  constant rom_c : word_t := init_rom(img);
begin
  process begin
    wait for 1 ns;
    assert rom_c(0) = x"0005" report "e0" severity failure;
    assert rom_c(1) = x"002A" report "e1" severity failure;
    assert rom_c(1023) = std_ulogic_vector(to_unsigned((1023*37+5) mod 65536, 16)) report "e1023" severity failure;
    std.env.finish;
    wait;
  end process;
end architecture;
