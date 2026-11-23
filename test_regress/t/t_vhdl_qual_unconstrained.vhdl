-- DESCRIPTION: Verilator: VHDL qualification with an unconstrained array mark
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A qualification with an unconstrained array mark keeps the operand's bounds
entity t is
  port (
    o : out boolean
  );
end entity;

architecture rtl of t is
  type mem_t is array (natural range <>) of bit_vector(1 downto 0);
begin
  process
    variable m : mem_t(0 to 1);
  begin
    o <= mem_t'(m) = m;
    wait;
  end process;
end architecture;
