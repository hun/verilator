-- DESCRIPTION: Verilator: a wait with a timeout has no model
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

entity t is end entity;
architecture a of t is
  signal x : bit;
begin
  process
  begin
    wait on x for 100 ns;
  end process;
end architecture;
